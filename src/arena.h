#ifndef ARENA_H
#define ARENA_H

typedef struct arena arena;
struct arena
{
	u8 *Buffer;
	u64 Capacity;
	u64 CurrentOffset;
	u64 PreviousOffset;
};

static void InitializeArena(arena *Arena);
static void ReleaseArena(arena *Arena);
static void *PushBytesToArena(arena *Arena, u64 Size);
static void *ReallocFromArena(arena *Arena, u64 Size);
static void ClearArena(arena *Arena);

#define PushStructToArena(Arena, Type) PushBytesToArena(Arena, sizeof(Type))
#define PushArrayToArena(Arena, Type, Count) PushBytesToArena(Arena, sizeof(Type) * (Count))

typedef struct arena_checkpoint arena_checkpoint;
struct arena_checkpoint
{
	arena *Arena;
	u64 CurrentOffset;
	u64 PreviousOffset;
};

static arena_checkpoint SetArenaCheckpoint(arena *Arena);
static void RestoreArenaFromCheckpoint(arena_checkpoint Checkpoint);

#endif
