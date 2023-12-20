#include <windows.h>

static void *
ReserveVirtualMemory(u64 BytesCount)
{
	void *Address = VirtualAlloc(0, BytesCount, MEM_RESERVE, PAGE_READWRITE);
	return Address;
}

static void
ReleaseVirtualMemory(void *Address, u64 BytesCount)
{
	(void)BytesCount;
	VirtualFree(Address, 0, MEM_RELEASE);
}

static b32
CommitVirtualMemory(void *Address, u64 BytesCount)
{
	b32 Success = VirtualAlloc(Address, BytesCount, MEM_COMMIT, PAGE_READWRITE) != 0;
	Assert(Success);
	return Success;
}

static void
DecommitVirtualMemory(void *Address, u64 BytesCount)
{
	VirtualFree(Address, BytesCount, MEM_DECOMMIT);
}
