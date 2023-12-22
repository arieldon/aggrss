#ifndef MEMORY_H
#define MEMORY_H

#ifdef DEBUG
#include <sanitizer/asan_interface.h>
#endif

// NOTE(ariel) Clang does not define `__SANITIZE_ADDRESS__`. It relies on
// __has_feature(). GCC defines the macro instead.
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define __ASAN_POISON_MEMORY_REGION(Address, BytesCount) __asan_poison_memory_region((Address), (BytesCount))
#define __ASAN_UNPOISON_MEMORY_REGION(Address, BytesCount) __asan_unpoison_memory_region((Address), (BytesCount))
#else
#define __ASAN_POISON_MEMORY_REGION(Address, BytesCount) ((void)(Address), (void)(BytesCount))
#define __ASAN_UNPOISON_MEMORY_REGION(Address, BytesCount) ((void)(Address), (void)(BytesCount))
#endif

static void *ReserveVirtualMemory(u64 BytesCount);
static void ReleaseVirtualMemory(void *Address, u64 BytesCount);
static b32 CommitVirtualMemory(void *Address, u64 BytesCount);
static void DecommitVirtualMemory(void *Address, u64 BytesCount);

#endif
