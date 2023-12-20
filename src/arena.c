enum
{
	ARENA_MEMORY_ALIGNMENT = 32,
	ARENA_PAGE_SIZE = KB(8),
};

static void
InitializeArena(arena *Arena)
{
	Arena->Buffer = ReserveVirtualMemory(GB(4));
	Arena->Capacity = ARENA_PAGE_SIZE;
	Arena->CurrentOffset = 0;
	Arena->PreviousOffset = 0;
	CommitVirtualMemory(Arena->Buffer, ARENA_PAGE_SIZE);
}

static void
ReleaseArena(arena *Arena)
{
	ClearArena(Arena);
	ReleaseVirtualMemory(Arena->Buffer, Arena->Capacity);
}

static uintptr
Align(uintptr Address)
{
	uintptr Padding = Address % ARENA_MEMORY_ALIGNMENT;
	if(Padding != 0)
	{
		Address += ARENA_MEMORY_ALIGNMENT - Padding;
	}
	return Address;
}

static void *
PushBytesToArena(arena *Arena, u64 Size)
{
	void *Address = 0;

	uintptr CurrentOffset = (uintptr)Arena->Buffer + (uintptr)Arena->CurrentOffset;
	uintptr AlignedOffset = Align(CurrentOffset);
	AlignedOffset -= (uintptr)Arena->Buffer;

	// TODO(ariel) Batch this to prevent unnecessary syscalls.
	while(AlignedOffset + Size > Arena->Capacity)
	{
		Arena->Capacity += ARENA_PAGE_SIZE;
		CommitVirtualMemory(Arena->Buffer, Arena->Capacity);
	}

	Arena->PreviousOffset = AlignedOffset;
	Arena->CurrentOffset = AlignedOffset + Size;
	Address = &Arena->Buffer[AlignedOffset];
	memset(Address, 0, Size);

	return Address;
}

static void *
ReallocFromArena(arena *Arena, u64 Size)
{
	Assert(((uintptr)Arena->Buffer + (uintptr)Arena->PreviousOffset) % ARENA_MEMORY_ALIGNMENT == 0);
	void *Address = 0;

	// TODO(ariel) Batch this to prevent unnecessary syscalls.
	while(Arena->PreviousOffset + Size > Arena->Capacity)
	{
		Arena->Capacity += ARENA_PAGE_SIZE;
		CommitVirtualMemory(Arena->Buffer, Arena->Capacity);
	}

	Arena->CurrentOffset = Arena->PreviousOffset + Size;
	Address = &Arena->Buffer[Arena->PreviousOffset];
	return Address;
}

static void
ClearArena(arena *Arena)
{
	Assert(Arena->Capacity >= ARENA_PAGE_SIZE);

	u8 *BufferOffset = Arena->Buffer + ARENA_PAGE_SIZE;
	u64 RemainingCapacity = Arena->Capacity - ARENA_PAGE_SIZE;
	DecommitVirtualMemory(BufferOffset, RemainingCapacity);

	Arena->Capacity = ARENA_PAGE_SIZE;
	Arena->CurrentOffset = Arena->PreviousOffset = 0;
}

static arena_checkpoint
SetArenaCheckpoint(arena *Arena)
{
	arena_checkpoint Checkpoint = {0};
	Checkpoint.Arena = Arena;
	Checkpoint.CurrentOffset = Arena->CurrentOffset;
	Checkpoint.PreviousOffset = Arena->PreviousOffset;
	return Checkpoint;
}

static void
RestoreArenaFromCheckpoint(arena_checkpoint Checkpoint)
{
	arena *Arena = Checkpoint.Arena;

	Arena->PreviousOffset = Checkpoint.PreviousOffset;
	Arena->CurrentOffset = Checkpoint.CurrentOffset;

	// NOTE(ariel) Compute the nearest page upper bound.
	usize Offset = MAX(Checkpoint.CurrentOffset, ARENA_PAGE_SIZE);
	usize Bump = Offset % ARENA_PAGE_SIZE;
	Offset += (ARENA_PAGE_SIZE - Bump) * (Bump != 0);
	Assert(Offset % ARENA_PAGE_SIZE == 0);

	u8 *BufferOffset = Arena->Buffer + Offset;
	usize RemainingCapacity = Arena->Capacity - Offset;
	Assert(RemainingCapacity < Arena->Capacity);

	DecommitVirtualMemory(BufferOffset, RemainingCapacity);
}
