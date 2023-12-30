#ifndef BASE_H
#define BASE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int64_t s64;
typedef size_t usize;
typedef ssize_t ssize;
typedef uintptr_t uintptr;
typedef intptr_t sintptr;
typedef s8 b8;
typedef s32 b32;
typedef float f32;
typedef double f64;

typedef struct string string;
struct string
{
	char *str;
	s32 len;
};
#define static_string_literal(s)  { .str = (char *)(s), .len = sizeof(s) - 1 }
#define string_literal(s) (string){ .str = (char *)(s), .len = sizeof(s) - 1 }

#define        global static
#define local_persist static
#define  thread_local _Thread_local

#define ARRAY_COUNT(x) (sizeof(x) / sizeof(*x))

#define MIN(a, b)      ((a) < (b) ? (a) : (b))
#define MAX(a, b)      ((a) > (b) ? (a) : (b))
#define CLAMP(x, a, b) MIN(b, MAX(a, x))

#define KB(n) (n << 10)
#define MB(n) (n << 20)
#define GB(n) (((u64)n) << 30)
#define TB(n) (((u64)n) << 40)

#define MEM_ZERO(addr, size)  memset((addr), 0, (size))
#define MEM_ZERO_STRUCT(addr) MEM_ZERO((addr), sizeof(*(addr)))
#define ZeroStruct(Address) (memset(Address, 0, sizeof(*Address)))

// TODO(ariel) Remove old definition of assert().
#ifdef DEBUG
#if __GNUC__
#	define assert(c) if (!(c)) __builtin_trap()
#elif _MSC_VER
#	define assert(c) if (!(c)) __debugbreak()
#else
#	define assert(c) if (!(c)) *(volatile int *)0 = 0
#endif
#else
#	define assert(ignore) ((void)0)
#endif

#define StaticAssert(X) _Static_assert(X, "")
#define Message(X) fprintf(stderr, "%s:%d: %s: assertion `%s` failed\n", __FILE__, __LINE__, __func__, #X)
#define Breakpoint() do { __asm__("int $3"); __asm__("nop"); } while (0)
#define AssertAlways(X) do { if (!(X)) { Message(X); Breakpoint(); } } while (0)
#ifdef DEBUG
#define Assert(X) AssertAlways(X)
#else
#define Assert(X)
#endif

#ifdef DEBUG
#	define breakpoint() __asm__("int $3")
#endif

// NOTE(ariel) Define `__has_feature` for compatibility between Clang and GCC.
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#ifndef __has_extension
#define __has_extension __has_feature
#endif

// NOTE(ariel) Use functions to read/write generation count to top 16-bits of
// pointer to reduce chance of ABA race condtions in CAS loops. Assume machine
// uses 64-bit pointers.
enum
{
	GENERATION_OFFSET = 0x30,
	GENERATION_MASK = 0xffffull << GENERATION_OFFSET,
};
static uintptr GetGeneration(void *Pointer) { return ((uintptr)Pointer & GENERATION_MASK) >> GENERATION_OFFSET; }
static void *GetAddress(void *Pointer) { return (void *)((uintptr)Pointer & ~GENERATION_MASK); }

#endif
