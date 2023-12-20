#ifndef MEMORY_H
#define MEMORY_H

static void *ReserveVirtualMemory(u64 BytesCount);
static void ReleaseVirtualMemory(void *Address, u64 BytesCount);
static b32 CommitVirtualMemory(void *Address, u64 BytesCount);
static void DecommitVirtualMemory(void *Address, u64 BytesCount);

#endif
