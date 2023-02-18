#ifndef BASE_H
#define BASE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

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

typedef struct {
	char *str;
	i32 len;
} String;
#define static_string_literal(s)  { .str = (char *)(s), .len = sizeof(s) - 1 }
#define string_literal(s) (String){ .str = (char *)(s), .len = sizeof(s) - 1 }

#define      internal static
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
#define SLL_TEMPLATE_PUSH_FRONT(f, l, n, next) \
	( \
		(f) == 0 ? \
		(f) = (l) = (n), (n)->next = 0 : \
		(n)->next = (f), (f) = (n) \
	)
#define SSL_PUSH_FRONT(f, l, n) \
	( \
		SLL_TEMPLATE_PUSH_BACK(f, l, n, next) \
	)

#define STACK_TEMPLATE_PUSH(f, n, next) \
	( \
		(n)->next = (f), (f) = (n) \
	)
#define STACK_PUSH(f, n) \
	( \
		STACK_TEMPLATE_PUSH(f, n, next) \
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
