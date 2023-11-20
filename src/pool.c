static void
InitializePool(pool *Pool)
{
	Assert(Pool->SlotSize);
	Assert(Pool->Capacity > Pool->SlotSize);
	Assert(Pool->Capacity % Pool->SlotSize == 0);
	Assert(Pool->Buffer);

	Pool->NextFreeSlot = NULL;
	ssize SlotsCount = Pool->Capacity / Pool->SlotSize;
	for(ssize Index = 1; Index < SlotsCount; Index += 1)
	{
		pool_slot *Slot = (pool_slot *)&Pool->Buffer[Index * Pool->SlotSize];
		Slot->Next = Pool->NextFreeSlot;
		Pool->NextFreeSlot = Slot;
	}
}

static void *
AllocatePoolSlot(pool *Pool)
{
	// NOTE(ariel) Return first slot as dummy buffer if out of memory.
	void *SlotAddress = Pool->Buffer;

	// NOTE(ariel) Exploit four-level paging by assigning top 16 bits to
	// generation count that reduces chance of ABA race condition to practically
	// implausible -- though still theoretically possible. It's highly unlikely
	// that other threads execute (2**16 + 1) iterations of this loop before this
	// thread pushes its changes to the stack.
	for(;;)
	{
		pool_slot *FreeSlotWithGeneration = Pool->NextFreeSlot;
		pool_slot *FreeSlot = GetAddress(FreeSlotWithGeneration);
		if(!FreeSlot)
		{
			break; // NOTE(ariel) Return first slot since out of memory.
		}

		uintptr OldGeneration = GetGeneration(FreeSlotWithGeneration);
		uintptr NewGeneration = OldGeneration + 1;
		pool_slot *NextSlot = (pool_slot *)((uintptr)FreeSlot->Next | (NewGeneration << GENERATION_OFFSET));

		if(atomic_compare_exchange_weak(&Pool->NextFreeSlot, &FreeSlotWithGeneration, NextSlot))
		{
			SlotAddress = (u8 *)FreeSlot;
			break;
		}
	}

#ifdef DEBUG
	if(SlotAddress == Pool->Buffer)
	{
		// TODO(ariel) Add additional debug metadata to pool for log in case of
		// error.
		fprintf(stderr, "pool (%p) out of memory\n", Pool);
	}
#endif
	memset(SlotAddress, 0, Pool->SlotSize);
	return SlotAddress;
}

static void
ReleasePoolSlot(pool *Pool, void *SlotAddress)
{
	pool_slot *NewFreeSlot = SlotAddress;

	void *LastSlot = Pool->Buffer + Pool->SlotSize*(Pool->Capacity/Pool->SlotSize - 1);
	if(SlotAddress >= (void *)Pool->Buffer && SlotAddress <= (void *)LastSlot)
	{
		for(;;)
		{
			pool_slot *OldFirstFreeSlot = Pool->NextFreeSlot;
			NewFreeSlot->Next = GetAddress(OldFirstFreeSlot);

			uintptr OldGeneration = GetGeneration(OldFirstFreeSlot);
			uintptr NewGeneration = OldGeneration + 1;
			NewFreeSlot = (pool_slot *)((uintptr)NewFreeSlot | (NewGeneration << GENERATION_OFFSET));

			if(atomic_compare_exchange_weak(&Pool->NextFreeSlot, &OldFirstFreeSlot, NewFreeSlot))
			{
				break;
			}
		}
	}
	else
	{
		Assert(!"unreachable");
	}
}
