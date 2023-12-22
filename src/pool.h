#ifndef POOL_H
#define POOL_H

typedef struct pool_slot pool_slot;
struct pool_slot
{
	pool_slot *Next;
};

typedef struct pool pool;
struct pool
{
	pool_slot *_Atomic NextFreeSlot;
	ssize SlotSize;
	ssize Capacity;
	u8 *Buffer;
};

static void InitializePool(pool *Pool);
static void *AllocatePoolSlot(pool *Pool);
static void ReleasePoolSlot(pool *Pool, void *SlotAddress);

#endif
