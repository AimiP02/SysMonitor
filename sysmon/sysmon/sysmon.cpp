#include "pch.h"
#include "fastmutex.h"
#include "SysmonCommon.h"
#include "Globals.h"

#define DRIVER_TAG 'nmys'

VOID SysmonUnload(IN PDRIVER_OBJECT DriverObject);
VOID OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);
VOID OnThreadNotify(_In_ HANDLE ProcessId, _In_ HANDLE ThreadId, _In_ BOOLEAN Create);
VOID OnImageNotify(_In_opt_ PUNICODE_STRING FullImageName, _In_ HANDLE ProcessId, _In_ PIMAGE_INFO ImageInfo);
NTSTATUS CompleteRequestAux(PIRP Irp, NTSTATUS status, ULONG_PTR info);
NTSTATUS SysmonCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS SysmonRead(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);

Globals g_Globals;

extern "C"
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);
	
	NTSTATUS status = STATUS_SUCCESS;
	bool hasSymLink = false, hasProcessMonitor = false, hasThreadMonitor = false, hasImageMonitor = false;

	g_Globals.Init();
	DriverObject->DriverUnload = SysmonUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = SysmonCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = SysmonRead;

	PDEVICE_OBJECT DeviceObject = nullptr;
	UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\Sysmon");
	UNICODE_STRING SymLinkName = RTL_CONSTANT_STRING(L"\\??\\Sysmon");

	status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status)) {
		DbgPrint("IoCreateDevice failed with status 0x%08X\n", status);
		goto exit;
	}
	DeviceObject->Flags |= DO_DIRECT_IO;

	status = IoCreateSymbolicLink(&SymLinkName, &DeviceName);
	if (!NT_SUCCESS(status)) {
		DbgPrint("IoCreateSymbolicLink failed with status 0x%08X\n", status);
		goto exit;
	}
	hasSymLink = true;

	status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
	if (!NT_SUCCESS(status)) {
		DbgPrint("PsSetCreateProcessNotifyRoutineEx failed with status 0x%08X\n", status);
		goto exit;
	}
	hasProcessMonitor = true;

	status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
	if (!NT_SUCCESS(status)) {
		DbgPrint("PsSetCreateNotifyRoutine failed with status 0x%08X\n", status);
		goto exit;
	}
	hasThreadMonitor = true;

	status = PsSetLoadImageNotifyRoutine(OnImageNotify);
	if (!NT_SUCCESS(status)) {
		DbgPrint("PsSetLoadImageNotifyRoutine failed with status 0x%08X\n", status);
		goto exit;
	}
	hasImageMonitor = true;

exit:
	if (!NT_SUCCESS(status)) {
		if (hasThreadMonitor)
			PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
		if (hasProcessMonitor)
			PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
		if (hasSymLink)
			IoDeleteSymbolicLink(&SymLinkName);
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}
	return status;
}

VOID OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
	UNREFERENCED_PARAMETER(Process);
	if (CreateInfo) {
		DbgPrint("Process Created: %d\n", HandleToULong(ProcessId));
		USHORT alloc_size = sizeof(FullItem<ProcessCreateInfo>);
		USHORT commandline_size = 0;
		if (CreateInfo->CommandLine) {
			commandline_size = CreateInfo->CommandLine->Length;
			alloc_size += commandline_size;
		}
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, alloc_size, DRIVER_TAG);
		if (info == nullptr) {
			DbgPrint("ProcessCreate Failed allocation\n");
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessCreate;
		item.ProcessId = HandleToULong(ProcessId);
		item.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);
		item.Size = sizeof(ProcessCreateInfo) + commandline_size;

		if (commandline_size > 0) {
			memcpy((UCHAR*)&item + sizeof(item), CreateInfo->CommandLine->Buffer, commandline_size);
			item.CommandLineLength = commandline_size / sizeof(WCHAR);
			item.CommandLineOffset = sizeof(item);
		}
		else {
			item.CommandLineLength = 0;
		}
		g_Globals.PushItem(&info->Entry);
	}
	else {
		DbgPrint("Process Exited: %d\n", HandleToULong(ProcessId));
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr) {
			DbgPrint("ProcessExit Failed allocation\n");
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.Size = sizeof(ProcessExitInfo);

		g_Globals.PushItem(&info->Entry);
	}
}

VOID OnThreadNotify(_In_ HANDLE ProcessId, _In_ HANDLE ThreadId, _In_ BOOLEAN Create) {
	auto size = sizeof(FullItem<ThreadCreateExitInfo>);
	auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, size, DRIVER_TAG);
	if (info == nullptr) {
		DbgPrint("ThreadCreateExit Failed allocation\n");
		return;
	}
	auto& item = info->Data;
	KeQuerySystemTimePrecise(&item.Time);
	item.Type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
	item.ProcessId = HandleToULong(ProcessId);
	item.ThreadId = HandleToULong(ThreadId);
	item.Size = sizeof(item);

	g_Globals.PushItem(&info->Entry);
}

VOID OnImageNotify(_In_opt_ PUNICODE_STRING FullImageName, _In_ HANDLE ProcessId, _In_ PIMAGE_INFO ImageInfo) {
	if (ProcessId == 0)
		return;
	auto size = sizeof(FullItem<ImageLoadInfo>);
	auto info = (FullItem<ImageLoadInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, size, DRIVER_TAG);
	if (info == nullptr) {
		DbgPrint("ImageLoad Failed allocation\n");
		return;
	}
	auto& item = info->Data;
	KeQuerySystemTimePrecise(&item.Time);
	item.Type = ItemType::ImageLoad;
	item.ProcessId = HandleToULong(ProcessId);
	item.ImageBase = (ULONG64)ImageInfo->ImageBase;
	item.ImageSize = (ULONG)ImageInfo->ImageSize;
	item.Size = sizeof(item);
	item.ImageFileName[0] = '7';

	if (ImageInfo->ExtendedInfoPresent) {
		auto exinfo = CONTAINING_RECORD(ImageInfo, IMAGE_INFO_EX, ImageInfo);
		PFLT_FILE_NAME_INFORMATION nameInfo = nullptr;
		auto status = FltGetFileNameInformationUnsafe(exinfo->FileObject, nullptr, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);
		if (NT_SUCCESS(status)) {
			wcscpy_s(item.ImageFileName, nameInfo->Name.Buffer);
			FltReleaseFileNameInformation(nameInfo);
		}
	}

	if (item.ImageFileName[0] == '7' && FullImageName) {
		wcscpy_s(item.ImageFileName, FullImageName->Buffer);
	}

	g_Globals.PushItem(&info->Entry);
}

NTSTATUS CompleteRequestAux(PIRP Irp, NTSTATUS status, ULONG_PTR info) {
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS SysmonCreateClose(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	return CompleteRequestAux(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS SysmonRead(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto length = stack->Parameters.Read.Length;
	NTSTATUS status = STATUS_SUCCESS;
	ULONG count = 0;
	
	NT_ASSERT(Irp->MdlAddress);

	auto buffer = (PUCHAR)MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer) {
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else {
		while (true) {
			if (IsListEmpty(&g_Globals.ItemsHead))
				break;
			
			auto entry = g_Globals.RemoveItem();
			if (entry == nullptr) {
				break;
			}
			auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
			auto size = info->Data.Size;

			if (length < size) {
				InsertHeadList(&g_Globals.ItemsHead, entry);
				break;
			}

			memcpy(buffer, &info->Data, size);
			length -= size;
			buffer += size;
			count += size;

			ExFreePool(info);
		}
	}
	return CompleteRequestAux(Irp, status, count);
}

VOID SysmonUnload(IN PDRIVER_OBJECT DriverObject) {
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
	PsRemoveLoadImageNotifyRoutine(OnImageNotify);

	LIST_ENTRY* entry;
	while ((entry = g_Globals.RemoveItem()) != nullptr) {
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));
	}

	UNICODE_STRING SymLinkName = RTL_CONSTANT_STRING(L"\\??\\Sysmon");
	IoDeleteSymbolicLink(&SymLinkName);
	IoDeleteDevice(DriverObject->DeviceObject);
}