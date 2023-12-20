#include <sys/mman.h>

static void *
ReserveVirtualMemory(u64 BytesCount)
{
	void *Address = mmap(NULL, BytesCount, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	AssertAlways(Address != MAP_FAILED);
	return Address;
}

static void
ReleaseVirtualMemory(void *Address, u64 BytesCount)
{
	munmap(Address, BytesCount);
}

static b32
CommitVirtualMemory(void *Address, u64 BytesCount)
{
	b32 Success = mprotect(Address, BytesCount, PROT_READ | PROT_WRITE) == 0;
	AssertAlways(Success);
	return Success;
}

static void
DecommitVirtualMemory(void *Address, u64 BytesCount)
{
	madvise(Address, BytesCount, MADV_FREE);
}
