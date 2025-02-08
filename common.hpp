#pragma once
#include <cstdint>
#if defined(_KERNEL_MODE)
#include <ntddk.h>
#else
#include <windows.h>
#endif
#define FoceWriteMemoryCTL CTL_CODE( FILE_DEVICE_UNKNOWN, 0xD00D, METHOD_BUFFERED, FILE_ANY_ACCESS )

struct WriteRequest
{
	uint64_t virt_addr;
	uint64_t data;
	uint32_t size;
};