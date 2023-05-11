#include <fltKernel.h>
#include <dontuse.h>

#include "Common.h"
#include "DelProtect.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

//---------------------------------------------------------------------------
//      Global variables
//---------------------------------------------------------------------------

Executables exes;

#define DRIVER_TAG 'ledp'

typedef struct _NULL_FILTER_DATA {

	//
	//  The filter handle that results from a call to
	//  FltRegisterFilter.
	//

	PFLT_FILTER FilterHandle;

} NULL_FILTER_DATA, *PNULL_FILTER_DATA;


/*************************************************************************
	Prototypes for the startup and unload routines used for
	this Filter.

	Implementation in nullFilter.c
*************************************************************************/

extern "C" NTSTATUS ZwQueryInformationProcess(
	_In_      HANDLE           ProcessHandle,
	_In_      PROCESSINFOCLASS ProcessInformationClass,
	_Out_     PVOID            ProcessInformation,
	_In_      ULONG            ProcessInformationLength,
	_Out_opt_ PULONG           ReturnLength
);

bool IsDeleteAllowed(const PEPROCESS Process);

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;
NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
);

NTSTATUS
DelProtectUnload(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
);

VOID
DelProtectDriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
);

NTSTATUS
CompleteRequestAux(
	_In_ PIRP Irp,
	_In_ NTSTATUS status,
	_In_ ULONG_PTR info
);

NTSTATUS
DelProtectCreateClose(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
);

NTSTATUS
DelProtectDeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
);

FLT_PREOP_CALLBACK_STATUS
DelProtectPreCreate(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

FLT_PREOP_CALLBACK_STATUS
DelProtectPreSetInformation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID* CompletionContext
);

NTSTATUS
NullQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
);

EXTERN_C_END

//
//  Structure that contains all the global data structures
//  used throughout NullFilter.
//

NULL_FILTER_DATA NullFilterData;

//
//  Assign text sections for each routine.
//

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, DelProtectUnload)
#pragma alloc_text(PAGE, NullQueryTeardown)
#endif


//
//  This defines what we want to filter with FltMgr
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
	{ IRP_MJ_CREATE, 
	  0, 
	  DelProtectPreCreate, 
	  nullptr },
	{ IRP_MJ_SET_INFORMATION,
	  0,
	  DelProtectPreSetInformation,
	  nullptr},
	{ IRP_MJ_OPERATION_END }
};

CONST FLT_REGISTRATION FilterRegistration = {

	sizeof(FLT_REGISTRATION),         //  Size
	FLT_REGISTRATION_VERSION,           //  Version
	0,                                  //  Flags

	NULL,                               //  Context
	Callbacks,                               //  Operation callbacks

	DelProtectUnload,                         //  FilterUnload

	NULL,                               //  InstanceSetup
	NullQueryTeardown,                  //  InstanceQueryTeardown
	NULL,                               //  InstanceTeardownStart
	NULL,                               //  InstanceTeardownComplete

	NULL,                               //  GenerateFileName
	NULL,                               //  GenerateDestinationFileName
	NULL                                //  NormalizeNameComponent

};


/*************************************************************************
	Filter initialization and unload routines.
*************************************************************************/

extern "C"
NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
)
/*++

Routine Description:

	This is the initialization routine for this miniFilter driver. This
	registers the miniFilter with FltMgr and initializes all
	its global data structures.

Arguments:

	DriverObject - Pointer to driver object created by the system to
		represent this driver.
	RegistryPath - Unicode string identifying where the parameters for this
		driver are located in the registry.

Return Value:

	Returns STATUS_SUCCESS.

--*/
{
	NTSTATUS status;

	UNREFERENCED_PARAMETER(RegistryPath);

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\DelProtect");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\DelProtect");
	DEVICE_OBJECT* deviceObject = nullptr;
	bool hasSymLink = false, hasDeviceObject = false;

	status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
	if (!NT_SUCCESS(status)) {
		DbgPrint("IoCreateDevice failed (error = %d)\n", status);
		goto exit;
	}
	hasDeviceObject = true;

	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		DbgPrint("IoCreateSymbolicLink failed (error = %d)\n", status);
		goto exit;
	}
	hasSymLink = true;
 
	//
	//  Register with FltMgr
	//

	status = FltRegisterFilter(DriverObject,
		&FilterRegistration,
		&NullFilterData.FilterHandle);

	FLT_ASSERT(NT_SUCCESS(status));

	if (!NT_SUCCESS(status)) {
		DbgPrint("FltRegisterFilter failed (error = %d)\n", status);
		goto exit;
	}

	status = FltStartFiltering(NullFilterData.FilterHandle);
	if (!NT_SUCCESS(status)) {
		DbgPrint("FltStartFiltering failed (error = %d)\n", status);
		goto exit;
	}

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = DelProtectCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DelProtectDeviceControl;
	DriverObject->DriverUnload = DelProtectDriverUnload;
	exes.Init();
	DbgPrint("DelProtect Driver Loaded\n");

exit:
	if (!NT_SUCCESS(status)) {
		if (NullFilterData.FilterHandle)
			FltUnregisterFilter(NullFilterData.FilterHandle);
		if (hasSymLink)
			IoDeleteSymbolicLink(&symLink);
		if (hasDeviceObject)
			IoDeleteDevice(deviceObject);
	}
	return status;
}

NTSTATUS
DelProtectUnload(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
/*++

Routine Description:

	This is the unload routine for this miniFilter driver. This is called
	when the minifilter is about to be unloaded. We can fail this unload
	request if this is not a mandatory unloaded indicated by the Flags
	parameter.

Arguments:

	Flags - Indicating if this is a mandatory unload.

Return Value:

	Returns the final status of this operation.

--*/
{
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	FltUnregisterFilter(NullFilterData.FilterHandle);

	return STATUS_SUCCESS;
}

NTSTATUS
NullQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
/*++

Routine Description:

	This is the instance detach routine for this miniFilter driver.
	This is called when an instance is being manually deleted by a
	call to FltDetachVolume or FilterDetach thereby giving us a
	chance to fail that detach request.

Arguments:

	FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
		opaque handles to this filter, instance and its associated volume.

	Flags - Indicating where this detach request came from.

Return Value:

	Returns the status of this operation.

--*/
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	return STATUS_SUCCESS;
}

FLT_PREOP_CALLBACK_STATUS
DelProtectPreCreate(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);

	if (Data->RequestorMode == KernelMode)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;

	auto&& params = Data->Iopb->Parameters.Create;
	auto returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (params.Options & FILE_DELETE_ON_CLOSE) {
		DbgPrint("Delete on close: %wZ\n", &Data->Iopb->TargetFileObject->FileName);
		if (!IsDeleteAllowed(PsGetCurrentProcess())) {
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			returnStatus = FLT_PREOP_COMPLETE;
			DbgPrint("Prevent delete from IRP_MJ_CREATE\n");
		}
	}
	return returnStatus;
}

FLT_PREOP_CALLBACK_STATUS
DelProtectPreSetInformation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID* CompletionContext
)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(CompletionContext);

	if (Data->RequestorMode == KernelMode)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;

	auto&& params = Data->Iopb->Parameters.SetFileInformation;
	auto returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (params.FileInformationClass != FileDispositionInformation &&
		params.FileInformationClass != FileDispositionInformationEx) {
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	auto info = (FILE_DISPOSITION_INFORMATION*)params.InfoBuffer;
	if (!info->DeleteFile)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;

	auto process = PsGetThreadProcess(Data->Thread);
	NT_ASSERT(process);

	DbgPrint("Delete on close: %wZ\n", &Data->Iopb->TargetFileObject->FileName);
	if (!IsDeleteAllowed(process)) {
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		returnStatus = FLT_PREOP_COMPLETE;
		DbgPrint("Prevent delete from IRP_MJ_CREATE by cmd.exe\n");
	}

	return returnStatus;
}

bool IsDeleteAllowed(const PEPROCESS Process) {
	bool currentProcess = PsGetCurrentProcess() == Process;
	HANDLE hProcess;
	if (currentProcess)
		hProcess = NtCurrentProcess();
	else {
		auto status = ObOpenObjectByPointer(Process, OBJ_KERNEL_HANDLE, nullptr, 0,
			nullptr, KernelMode, &hProcess);
		if (!NT_SUCCESS(status)) {
			return true;
		}
	}

	auto size = 300;
	bool allowDelete = true;
	auto processName = (UNICODE_STRING*)ExAllocatePool2(POOL_FLAG_PAGED, size, DRIVER_TAG);

	if (processName) {
		RtlZeroMemory(processName, size);
		auto status = ZwQueryInformationProcess(hProcess, ProcessImageFileName, processName,
			size - sizeof(WCHAR), nullptr);
		if (NT_SUCCESS(status)) {
			DbgPrint("Delete operation from %wZ\n", processName);

			auto exeName = wcsrchr(processName->Buffer, L'\\');
			NT_ASSERT(exeName);
			DbgPrint("Executable name: %ws\n", exeName + 1);

			if (processName->Length > 0 && exes.FindExecutable(exeName + 1)) {
				allowDelete = false;
			}
		}
		ExFreePool(processName);
	}
	if (!currentProcess)
		ZwClose(hProcess);
	return allowDelete;
}

VOID
DelProtectDriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
)
{
	exes.ClearAll();
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\DelProtect");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS
CompleteRequestAux(
	_In_ PIRP Irp,
	_In_ NTSTATUS status,
	_In_ ULONG_PTR info
)
{
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = info;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS
DelProtectCreateClose(
	_In_ PDEVICE_OBJECT,
	_In_ PIRP Irp
)
{
	return CompleteRequestAux(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
DelProtectDeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;
	auto length = stack->Parameters.DeviceIoControl.InputBufferLength;

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_DELPROTECT_ADD_EXE: {
		auto name = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
		if (name == nullptr || length == 0) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		if (exes.FindExecutable(name)) {
			break;
		}

		AutoLock<FastMutex> lock(exes.ExeNamesLock);
		if (exes.ExeNamesCount == MaxExecutables) {
			status = STATUS_TOO_MANY_NAMES;
			break;
		}

		for (int i = 0; i < MaxExecutables; i++) {
			if (exes.ExeNames[i] == nullptr) {
				auto len = (wcslen(name) + 1) * sizeof(WCHAR);
				auto buffer = (WCHAR*)ExAllocatePool2(POOL_FLAG_PAGED, len, DRIVER_TAG);
				if (!buffer) {
					status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}
				wcscpy_s(buffer, len / sizeof(WCHAR), name);
				exes.ExeNames[i] = buffer;
				exes.ExeNamesCount++;
				break;
			}
		}
		DbgPrint("IOCTL_DELPROTECT_ADD_EXE Request received\n");
		break;
	}
	case IOCTL_DELPROTECT_REMOVE_EXE: {
		auto name = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
		if (name == nullptr || length == 0) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		AutoLock<FastMutex> lock(exes.ExeNamesLock);
		auto hasFound = false;
		
		for (int i = 0; i < MaxExecutables; i++) {
			if (!_wcsicmp(exes.ExeNames[i], name)) {
				ExFreePool(exes.ExeNames[i]);
				exes.ExeNamesCount--;
				exes.ExeNames[i] = nullptr;
				hasFound = true;
				break;
			}
		}

		if (!hasFound)
			status = STATUS_NOT_FOUND;
		DbgPrint("IOCTL_DELPROTECT_REMOVE_EXE Request received\n");
		break;
	}
	case IOCTL_DELPROTECT_CLEAR: {
		exes.ClearAll();
		DbgPrint("IOCTL_DELPROTECT_CLEAR Request received\n");
		break;
	}

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	return CompleteRequestAux(Irp, status, 0);
}