#ifndef ARENA_H
#define ARENA_H

typedef struct Arena Arena;
struct Arena
{
	u8 *buf;
	usize cap;
	usize prev;
	usize curr;
};

static void arena_init(Arena *arena);
static void arena_release(Arena *arena);
static void *arena_alloc(Arena *arena, usize size);
static void *arena_realloc(Arena *arena, usize size);
static void arena_clear(Arena *arena);

typedef struct Arena_Checkpoint Arena_Checkpoint;
struct Arena_Checkpoint
{
	Arena *arena;
	usize prev;
	usize curr;
};

static Arena_Checkpoint arena_checkpoint_set(Arena *arena);
static void arena_checkpoint_restore(Arena_Checkpoint checkpoint);

#endif
