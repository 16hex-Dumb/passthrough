#include <ntddk.h>
#include <cstdint>
#include "../common.hpp"
#define dbg_printf(...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, __VA_ARGS__);

UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\WriteMemoryDriver");
UNICODE_STRING DosSymlink = RTL_CONSTANT_STRING(L"\\DosDevices\\WriteMemoryDriver");

void DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
	// Delete sym link, and device object.
	IoDeleteSymbolicLink(&DosSymlink);
	IoDeleteDevice(DriverObject->DeviceObject);
	dbg_printf("Driver Unloaded.\n");
}

bool force_write_memory(WriteRequest* req)
{
	PHYSICAL_ADDRESS phys_addr = MmGetPhysicalAddress(reinterpret_cast<void*>(req->virt_addr));
	dbg_printf("Physical Address: %llx\n", phys_addr.QuadPart);
	if (phys_addr.QuadPart == 0)
	{
		dbg_printf("Failed to get physical address.\n");
		return false;
	}
	auto maped_addr = reinterpret_cast<uintptr_t*>(MmMapIoSpace(phys_addr, req->size, MmNonCached));
	if (!maped_addr)
	{
		dbg_printf("Failed to map physical memory.\n");
		return false;
	}

	memcpy(maped_addr, &req->data, req->size);

	MmUnmapIoSpace(maped_addr, req->size);
	return true;
}

NTSTATUS Dispatch(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
	ULONG IoControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;
	ULONG InputBufferLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
	ULONG OutputBufferLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
	void* SystemBuffer = Irp->AssociatedIrp.SystemBuffer;

	// Check if the major function is for device control, if not then return success.
	if (Stack->MajorFunction != IRP_MJ_DEVICE_CONTROL)
	{
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	// Basic sanity checks.
	if (InputBufferLength != sizeof(WriteRequest) || OutputBufferLength != 4 || IoControlCode != FoceWriteMemoryCTL)
	{
		Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_PARAMETER;
	}

	// Using a switch case here, as I had a bunch of cases here which are now stripped for publication.
	// Feel free to use an if else here, if that's what you prefer.
	switch (IoControlCode)
	{
	case FoceWriteMemoryCTL:
	{
		*reinterpret_cast<uint32_t*>(SystemBuffer) = force_write_memory(reinterpret_cast<WriteRequest*>(SystemBuffer)) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
		break;
	}
	}

	// Everything went well, return success.
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 4;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

extern "C"
NTSTATUS
NTAPI
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS status = STATUS_SUCCESS;

	// Create the device object.
	PDEVICE_OBJECT DeviceObject;
	if (!NT_SUCCESS(status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject)))
	{
		dbg_printf("Failed to create device object status: %lX\n", status);
		return status;
	}

	// Create the dos symbolic link, so our client can communicate.
	if (!NT_SUCCESS(status = IoCreateSymbolicLink(&DosSymlink, &DeviceName)))
	{
		dbg_printf("Failed to create sym link status: %lX\n", status);
		IoDeleteDevice(DeviceObject);
		return status;
	}
	// Set up the dispatch functions, and unload callback.
	DriverObject->MajorFunction[IRP_MJ_CREATE] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = Dispatch;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Dispatch;
	DriverObject->DriverUnload = DriverUnload;
	dbg_printf("Driver Loaded.\n");
	return STATUS_SUCCESS;
}