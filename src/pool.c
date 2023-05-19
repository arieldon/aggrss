#include <pthread.h>
#include <sys/mman.h>

#include "base.h"
#include "pool.h"

enum { N_PAGES = 4 };

void
init_pool(Pool *pool)
{
	assert(pool->page_size);
	assert(pool->slot_size);
	assert(pool->slot_size >= sizeof(Slot));
	assert(pool->page_size > pool->slot_size);
	assert(pool->page_size % pool->slot_size == 0);

	pool->total_size = N_PAGES * pool->page_size;
	u8 *buffer = mmap(NULL, pool->total_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buffer == MAP_FAILED)
	{
		abort();
	}
	if (mprotect(buffer, pool->page_size, PROT_READ | PROT_WRITE) == -1)
	{
		abort();
	}

	pool->buffer = buffer;
	pool->capacity = pool->page_size;

	// NOTE(ariel) Free all slots by default.
	usize n_slots = pool->page_size / pool->slot_size;
	for (usize i = 0; i < n_slots; ++i)
	{
		Slot *slot = (Slot *)&pool->buffer[i * pool->slot_size];
		slot->next = pool->first_free_slot;
		pool->first_free_slot = slot;
	}

#ifdef DEBUG
	{
		usize count = 0;
		for (Slot *slot = pool->first_free_slot; slot; slot = slot->next)
		{
			++count;
		}
		assert(count == n_slots);
	}
#endif

	pthread_mutex_init(&pool->big_lock, 0);
}

void *
get_slot(Pool *pool)
{
	void *slot_address = 0;

	pthread_mutex_lock(&pool->big_lock);
	{
		if (!pool->first_free_slot)
		{
			usize previous_capacity = pool->capacity;

			pool->capacity += pool->page_size;
			if (pool->capacity > pool->total_size)
			{
				abort();
			}
			if (mprotect(pool->buffer, pool->capacity, PROT_READ | PROT_WRITE) == -1)
			{
				abort();
			}

			// NOTE(ariel) Push new slots onto free list.
			u8 *pool_end = pool->buffer + pool->capacity;
			for (
				u8 *offset = pool->buffer + previous_capacity;
				offset < pool_end;
				offset += pool->slot_size)
			{
				Slot *slot = (Slot *)offset;
				slot->next = pool->first_free_slot;
				pool->first_free_slot = slot;
			}
		}

		Slot *slot = pool->first_free_slot;
		pool->first_free_slot = slot->next;
		slot_address = slot;
	}
	pthread_mutex_unlock(&pool->big_lock);

	assert(slot_address);
	MEM_ZERO(slot_address, pool->slot_size);
	return slot_address;
}

void
return_slot(Pool *pool, void *slot_address)
{
	pthread_mutex_lock(&pool->big_lock);
	{
		Slot *slot = slot_address;

		void *pool_end = pool->buffer + pool->capacity;
		if (slot_address > pool_end || slot_address < (void *)pool->buffer)
		{
			abort();
		}

		slot->next = pool->first_free_slot;
		pool->first_free_slot = slot;
	}
	pthread_mutex_unlock(&pool->big_lock);
}

void
free_pool(Pool *pool)
{
	for (;;)
	{
		b32 success = pthread_mutex_trylock(&pool->big_lock) == 0;
		if (success)
		{
			pthread_mutex_unlock(&pool->big_lock);
			pthread_mutex_destroy(&pool->big_lock);
			break;
		}
	}
	munmap(pool->buffer, pool->capacity);
}
