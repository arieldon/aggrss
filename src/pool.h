#ifndef POOL_H
#define POOL_H

#include <pthread.h>

#include "base.h"

typedef struct Slot Slot;
struct Slot
{
	Slot *next;
};

typedef struct Pool Pool;
struct Pool
{
	u8 *buffer;
	usize capacity;
	usize page_size;
	usize slot_size;
	usize total_size;
	Slot *first_free_slot;
	pthread_mutex_t big_lock;
};

void init_pool(Pool *pool);
void *get_slot(Pool *pool);
void return_slot(Pool *pool, void *slot_address);
void free_pool(Pool *pool);

#endif
