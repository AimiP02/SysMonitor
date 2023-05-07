#include "pch.h"
#include "FastMutex.h"
#include "ProcProtect.h"
#include "ProcProtectCommon.h"

#define DRIVER_NAME L"\\Device\\ProcProtect"
#define SYMLINK_NAME L"\\??\\ProcProtect"

VOID ProcProtectUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS CompleteRequestAux(PIRP Irp, NTSTATUS status, ULONG_PTR info);
NTSTATUS ProcProtectCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ProcProtectDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION OperationInformation);

Globals g_Data;

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	auto status = STATUS_SUCCESS;
	UNICODE_STRING devName = RTL_CONSTANT_STRING(DRIVER_NAME);
	UNICODE_STRING symName = RTL_CONSTANT_STRING(SYMLINK_NAME);
	PDEVICE_OBJECT DeviceObject = nullptr;
	bool hasDevice = false, hasSymLink = false, hasObRegister = false;

	g_Data.Init();

	OB_OPERATION_REGISTRATION operations[] = {
		{
		PsProcessType,
		OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE,
		OnPreOpenProcess,
		nullptr
		}
	};

	OB_CALLBACK_REGISTRATION reg = {
		OB_FLT_REGISTRATION_VERSION,
		1,
		RTL_CONSTANT_STRING(L"12345.6171"),
		nullptr,
		operations
	};

	status = ObRegisterCallbacks(&reg, &g_Data.RegHandle);
	if (!NT_SUCCESS(status)) {
		DbgPrint("ObRegisterCallbacks failed with status 0x%08X\n", status);
		goto exit;
	}
	hasObRegister = true;

	status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if (!NT_SUCCESS(status)) {
		DbgPrint("IoCreateDevice failed with status 0x%08X\n", status);
		goto exit;
	}
	hasDevice = true;
	//DeviceObject->Flags |= DO_BUFFERED_IO;

	status = IoCreateSymbolicLink(&symName, &devName);
	if (!NT_SUCCESS(status)) {
		DbgPrint("IoCreateSymbolicLink failed with status 0x%08X\n", status);
		goto exit;
	}
	hasSymLink = true;

	DriverObject->DriverUnload = ProcProtectUnload;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DriverObject->MajorFunction[IRP_MJ_CREATE] = ProcProtectCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ProcProtectDeviceControl;
	DbgPrint("ProcProtect Driver Loaded.");

exit:
	if (!NT_SUCCESS(status)) {
		if (hasSymLink)
			IoDeleteSymbolicLink(&symName);
		if (hasDevice)
			IoDeleteDevice(DeviceObject);
		if (hasObRegister)
			ObUnRegisterCallbacks(g_Data.RegHandle);
	}
	return status;
}

NTSTATUS CompleteRequestAux(PIRP Irp, NTSTATUS status, ULONG_PTR info) {
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS ProcProtectCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	return CompleteRequestAux(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS ProcProtectDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
	UNREFERENCED_PARAMETER(DeviceObject);
	
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;
	auto length = 0;

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_PROCESS_PROTECT_BY_PID: {
		auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (size % sizeof(ULONG) != 0) {
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		auto data = (ULONG*)Irp->AssociatedIrp.SystemBuffer;

		AutoLock<FastMutex> lock(g_Data.Lock);

		for (ULONG i = 0; i < size / sizeof(ULONG); i++) {
			auto pid = data[i];
			if (pid == 0) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (g_Data.FindProcess(pid)) {
				continue;
			}
			if (g_Data.PidsCount == maxPids) {
				status = STATUS_TOO_MANY_CONTEXT_IDS;
				break;
			}
			if (!g_Data.AddProcess(pid)) {
				status = STATUS_UNSUCCESSFUL;
				break;
			}

			length += sizeof(ULONG);
		}
		break;
	}
	case IOCTL_PROCESS_UNPROTECT_BY_PID: {
		auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (size % sizeof(ULONG) != 0) {
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		auto data = (ULONG*)Irp->AssociatedIrp.SystemBuffer;

		AutoLock<FastMutex> lock(g_Data.Lock);

		for (ULONG i = 0; i < size / sizeof(ULONG); i++) {
			auto pid = data[i];
			if (pid == 0) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (!g_Data.RemoveProcess(pid)) {
				status = STATUS_UNSUCCESSFUL;
				break;
			}

			length += sizeof(ULONG);

			if (g_Data.PidsCount == 0) {
				break;
			}
		}
		break;
	}
	case IOCTL_PROCESS_PROTECT_CLEAR: {
		AutoLock<FastMutex> lock(g_Data.Lock);
		memset(&g_Data.Pids, 0, sizeof(g_Data.Pids));
		g_Data.PidsCount = 0;
		break;
	}

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	return CompleteRequestAux(Irp, status, length);
}

VOID ProcProtectUnload(PDRIVER_OBJECT DriverObject) {
	ObUnRegisterCallbacks(g_Data.RegHandle);
	
	UNICODE_STRING symName = RTL_CONSTANT_STRING(SYMLINK_NAME);
	IoDeleteSymbolicLink(&symName);

	IoDeleteDevice(DriverObject->DeviceObject);
}

OB_PREOP_CALLBACK_STATUS
OnPreOpenProcess(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION OperationInformation) {
	UNREFERENCED_PARAMETER(RegistrationContext);
	if (OperationInformation->KernelHandle)
		return OB_PREOP_SUCCESS;

	auto process = (PEPROCESS)OperationInformation->Object;
	auto pid = HandleToULong(PsGetProcessId(process));

	AutoLock<FastMutex> lock(g_Data.Lock);
	if (g_Data.FindProcess(pid)) {
		OperationInformation->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_TERMINATE;
	}

	return OB_PREOP_SUCCESS;
}