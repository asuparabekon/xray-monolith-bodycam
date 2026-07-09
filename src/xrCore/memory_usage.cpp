#include "stdafx.h"
#include <malloc.h>
#include <errno.h>
#include <psapi.h>

namespace
{
using get_process_memory_info_fn = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);

get_process_memory_info_fn resolve_process_memory_info()
{
	static get_process_memory_info_fn fn = []
	{
		HMODULE kernel = GetModuleHandleA("kernel32.dll");
		if (kernel)
		{
			if (auto* proc = reinterpret_cast<get_process_memory_info_fn>(GetProcAddress(kernel, "K32GetProcessMemoryInfo")))
				return proc;
		}

		HMODULE psapi = LoadLibraryA("psapi.dll");
		if (psapi)
		{
			if (auto* proc = reinterpret_cast<get_process_memory_info_fn>(GetProcAddress(psapi, "GetProcessMemoryInfo")))
				return proc;
		}

		return static_cast<get_process_memory_info_fn>(nullptr);
	}();

	return fn;
}

bool query_process_memory_usage(size_t& bytes_used)
{
	PROCESS_MEMORY_COUNTERS_EX counters = {};
	counters.cb = sizeof(counters);

	get_process_memory_info_fn fn = resolve_process_memory_info();
	if (!fn || !fn(GetCurrentProcess(), reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters), sizeof(counters)))
		return false;

	bytes_used = counters.PrivateUsage ? counters.PrivateUsage : counters.WorkingSetSize;
	return true;
}
}

XRCORE_API void vminfo(size_t* _free, size_t* reserved, size_t* committed)
{
	MEMORY_BASIC_INFORMATION memory_info;
	memory_info.BaseAddress = 0;
	*_free = *reserved = *committed = 0;
	while (VirtualQuery(memory_info.BaseAddress, &memory_info, sizeof(memory_info)))
	{
		switch (memory_info.State)
		{
		case MEM_FREE:
			*_free += memory_info.RegionSize;
			break;
		case MEM_RESERVE:
			*reserved += memory_info.RegionSize;
			break;
		case MEM_COMMIT:
			*committed += memory_info.RegionSize;
			break;
		}
		memory_info.BaseAddress = (char*)memory_info.BaseAddress + memory_info.RegionSize;
	}
}

xrCriticalSection mem_lock;
XRCORE_API void log_vminfo()
{
    xrCriticalSectionGuard g(mem_lock);
	PROF_EVENT("log_vminfo");
	size_t w_free, w_reserved, w_committed;
	vminfo(&w_free, &w_reserved, &w_committed);
	Msg(
		"* [win32]: free[%lld K], reserved[%lld K], committed[%lld K]",
		w_free / 1024,
		w_reserved / 1024,
		w_committed / 1024
	);
}

size_t xrMemory::mem_usage(bool assert)
{
    xrCriticalSectionGuard g(mem_lock);
	PROF_EVENT("mem_usage");

	size_t process_bytes = 0;
	if (query_process_memory_usage(process_bytes))
		return process_bytes;

	_HEAPINFO hinfo = {};
	int status;
	size_t bytesUsed = 0;
	while ((status = _heapwalk(&hinfo)) == _HEAPOK)
	{
		if (hinfo._useflag == _USEDENTRY)
			bytesUsed += hinfo._size;
	}
	switch (status)
	{
	case _HEAPEMPTY:
		break;
	case _HEAPEND:
		break;
	case _HEAPBADPTR:
		if (assert) Msg("! xrMemory::mem_usage fallback heapwalk failed: bad pointer to heap");
		break;
	case _HEAPBADBEGIN:
        if (assert) Msg("! xrMemory::mem_usage fallback heapwalk failed: bad start of heap");
		break;
	case _HEAPBADNODE:
        if (assert) Msg("! xrMemory::mem_usage fallback heapwalk failed: bad node in heap");
		break;
	}
	return bytesUsed;
}
