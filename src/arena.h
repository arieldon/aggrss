#ifndef ARENA_H
#define ARENA_H

#include "base.h"

typedef struct Arena Arena;
struct Arena
{
	u8 *buf;
	usize cap;
	usize prev;
	usize curr;
};

void arena_init(Arena *arena);
void arena_release(Arena *arena);
void *arena_alloc(Arena *arena, usize size);
void *arena_realloc(Arena *arena, usize size);
void arena_clear(Arena *arena);

typedef struct Arena_Checkpoint Arena_Checkpoint;
struct Arena_Checkpoint
{
	Arena *arena;
	usize prev;
	usize curr;
};

Arena_Checkpoint arena_checkpoint_set(Arena *arena);
void arena_checkpoint_restore(Arena_Checkpoint checkpoint);

#endif
