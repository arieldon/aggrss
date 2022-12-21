#ifndef BASE_H
#define BASE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef  uint8_t    u8;
typedef   int8_t    i8;
typedef uint16_t   u16;
typedef  int16_t   i16;
typedef uint32_t   u32;
typedef  int32_t   i32;
typedef uint64_t   u64;
typedef  int64_t   i64;
typedef   size_t usize;
typedef  ssize_t isize;
typedef    float   f32;
typedef   double   f64;

#define internal static
#define   global static

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

#define KB(n) (n << 10)
#define MB(n) (n << 20)
#define GB(n) (((u64)n) << 30)
#define TB(n) (((u64)n) << 40)

#define MEM_ZERO(addr, size)  memset((addr), 0, (size))
#define MEM_ZERO_STRUCT(addr) MEM_ZERO((addr), sizeof(*(addr)))

#define DLL_TEMPLATE_PUSH_BACK(f, l, n, next, prev) \
	( \
		(f) == 0 ? \
		((f) = (l) = (n), (n)->next = (n)->prev = 0) : \
		((n)->prev = (l), (l)->next = (n), (l) = (n), (n)->next = 0) \
	)
#define DLL_PUSH_BACK(f, l, n) \
	( \
		DLL_TEMPLATE_PUSH_BACK(f, l, n, next, prev) \
	)
#define DLL_PUSH_FRONT(f, l, n) \
	( \
		DLL_TEMPLATE_PUSH_BACK(l, f, n, prev, next) \
	)

#define SLL_TEMPLATE_PUSH_BACK(f, l, n, next) \
	( \
		(f) == 0 ? \
		(f) = (l) = (n) : \
		((l)->next = (n), (l) = (n)), \
		(n)->next = 0 \
	)
#define SLL_PUSH_BACK(f, l, n) \
	( \
		SLL_TEMPLATE_PUSH_BACK(f, l, n, next) \
	)

#ifdef DEBUG
#if __GNUC__
#	define assert(c) if (!(c)) __builtin_trap()
#elif _MSC_VER
#	define assert(c) if (!(c)) __debugbreak()
#else
#	define assert(c) if (!(c)) *(volatile int *)0 = 0
#endif
#else
#	include <assert.h>
#endif

#ifdef DEBUG
#	define breakpoint() asm("int $3")
#endif

#endif
