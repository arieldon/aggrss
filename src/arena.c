#include <string.h>

#include <sys/mman.h>

#include "arena.h"
#include "base.h"
#include "err.h"

enum
{
	MEMORY_ALIGNMENT = (sizeof(void *) * 2),
	PAGE_SIZE = KB(8),
};

void
arena_init(Arena *arena)
{
	u8 *buf = mmap(NULL, GB(4), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED)
	{
		abort();
	}
	if (mprotect(buf, PAGE_SIZE, PROT_READ | PROT_WRITE) == -1)
	{
		abort();
	}

	arena->buf = buf;
	arena->cap = PAGE_SIZE;
	arena->curr = 0;
	arena->prev = 0;
}

void
arena_release(Arena *arena)
{
	arena_clear(arena);
	(void)munmap(arena->buf, arena->cap);
}

internal uintptr_t
align(uintptr_t p)
{
	uintptr_t m = p % MEMORY_ALIGNMENT;
	if (m != 0)
	{
		p += MEMORY_ALIGNMENT - m;
	}
	return p;
}

void *
arena_alloc(Arena *arena, usize size)
{
	uintptr_t curr = (uintptr_t)arena->buf + (uintptr_t)arena->curr;
	uintptr_t offset = align(curr);
	offset -= (uintptr_t)arena->buf;

alloc:
	if (offset + size <= arena->cap)
	{
		arena->prev = offset;
		arena->curr = offset + size;
		void *p = &arena->buf[offset];
		memset(p, 0, size);
		return p;
	}
	else
	{
		arena->cap += PAGE_SIZE;
		if (mprotect(arena->buf, arena->cap, PROT_READ | PROT_WRITE) == -1)
		{
			abort();
		}
		goto alloc;
	}

	return 0;
}

void *
arena_realloc(Arena *arena, usize size)
{
	assert(((uintptr_t)arena->buf + (uintptr_t)arena->prev) % MEMORY_ALIGNMENT == 0);

alloc:
	if (arena->prev + size <= arena->cap)
	{
		arena->curr = arena->prev + size;

		void *p = &arena->buf[arena->prev];
		return p;
	}
	else
	{
		arena->cap += PAGE_SIZE;
		if (mprotect(arena->buf, arena->cap, PROT_READ | PROT_WRITE) == -1)
		{
			abort();
		}
		goto alloc;
	}

	return 0;
}

void
arena_clear(Arena *arena)
{
	assert(arena->cap >= PAGE_SIZE);
	u8 *buffer_offset = arena->buf + PAGE_SIZE;
	usize remaining_capacity = arena->cap - PAGE_SIZE;
	if (madvise(buffer_offset, remaining_capacity, MADV_FREE) == -1)
	{
		abort();
	}
	arena->cap = PAGE_SIZE;
	arena->curr = arena->prev = 0;
}

Arena_Checkpoint
arena_checkpoint_set(Arena *arena)
{
	Arena_Checkpoint checkpoint =
	{
		.arena = arena,
		.prev = arena->prev,
		.curr = arena->curr,
	};
	return checkpoint;
}

void
arena_checkpoint_restore(Arena_Checkpoint checkpoint)
{
	Arena *arena = checkpoint.arena;

	arena->prev = checkpoint.prev;
	arena->curr = checkpoint.curr;

	// NOTE(ariel) Compute the nearest page upper bound.
	usize offset = MAX(checkpoint.curr, PAGE_SIZE);
	usize bump = offset % PAGE_SIZE;
	offset += (PAGE_SIZE - bump) * (bump != 0);
	assert(offset % PAGE_SIZE == 0);

	u8 *buffer_offset = arena->buf + offset;
	usize remaining_capacity = arena->cap - offset;
	assert(remaining_capacity < arena->cap);
	if (madvise(buffer_offset, remaining_capacity, MADV_FREE) == -1)
	{
		abort();
	}
}
