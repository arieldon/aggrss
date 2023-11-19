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
	pool_slot *_Atomic NextFreeSlot; // NOTE(ariel) Store an atomic pointer.

	ssize SlotSize;
	ssize Capacity;
	u8 *Buffer;
};

static void InitializePool(pool *Pool);
static void *AllocatePoolSlot(pool *Pool); // FIXME(ariel) Does the compiler generate different assembly if this is a u8 *?
static void ReleasePoolSlot(pool *Pool, void *SlotAddress);

#endif
