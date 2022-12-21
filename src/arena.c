#include <string.h>

#include <sys/mman.h>

#include "arena.h"
#include "base.h"
#include "err.h"

enum { MEMORY_ALIGNMENT = (sizeof(void *) * 2) };

void
arena_init(Arena *arena)
{
	u8 *buf = mmap(NULL, GB(4), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) abort();
	if (mprotect(buf, KB(8), PROT_READ | PROT_WRITE) == -1) abort();

	arena->buf = buf;
	arena->cap = KB(8);
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
	if (m != 0) p += MEMORY_ALIGNMENT - m;
	return p;
}

void *
arena_alloc(Arena *arena, usize size)
{
	uintptr_t curr = (uintptr_t)arena->buf + (uintptr_t)arena->curr;
	uintptr_t offset = align(curr);
	offset -= (uintptr_t)arena->buf;

alloc:
	if (offset + size <= arena->cap) {
		arena->prev = offset;
		arena->curr = offset + size;
		void *p = &arena->buf[offset];
		memset(p, 0, size);
		return p;
	} else {
		arena->cap += KB(4);
		if (mprotect(arena->buf, arena->cap, PROT_READ | PROT_WRITE) == -1) abort();
		goto alloc;
	}

	return 0;
}

void *
arena_realloc(Arena *arena, usize size)
{
	assert(((uintptr_t)arena->buf + (uintptr_t)arena->prev) % MEMORY_ALIGNMENT == 0);

alloc:
	if (arena->prev + size <= arena->cap) {
		arena->curr = arena->prev + size;

		void *p = &arena->buf[arena->prev];
		return p;
	} else {
		arena->cap += KB(4);
		if (mprotect(arena->buf, arena->cap, PROT_READ | PROT_WRITE) == -1) abort();
		goto alloc;
	}

	return 0;
}

void
arena_clear(Arena *arena)
{
	arena->curr = arena->prev = 0;
}

Arena_Checkpoint
arena_checkpoint_set(Arena *arena)
{
	return (Arena_Checkpoint){
		.arena = arena,
		.prev = arena->prev,
		.curr = arena->curr,
	};
}

void
arena_checkpoint_restore(Arena_Checkpoint checkpoint)
{
	checkpoint.arena->prev = checkpoint.prev;
	checkpoint.arena->curr = checkpoint.curr;
}
