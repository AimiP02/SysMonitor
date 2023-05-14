#include <fltKernel.h>
#include <dontuse.h>

#include "Common.h"
#include "DelProtect.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

//---------------------------------------------------------------------------
//      Global variables
//---------------------------------------------------------------------------

Directorys dirs;

typedef struct _DIRPROTECT_FILTER_DATA {
	PFLT_FILTER FilterHandle;
} DIRPROTECT_FILTER_DATA;

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

bool IsDeleteAllowed(_Inout_ PFLT_CALLBACK_DATA Data);

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

DIRPROTECT_FILTER_DATA DirFilterData;

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

	status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 
		0, FALSE, &deviceObject);
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
		&DirFilterData.FilterHandle);

	FLT_ASSERT(NT_SUCCESS(status));

	if (!NT_SUCCESS(status)) {
		DbgPrint("FltRegisterFilter failed (error = %d)\n", status);
		goto exit;
	}

	status = FltStartFiltering(DirFilterData.FilterHandle);
	if (!NT_SUCCESS(status)) {
		DbgPrint("FltStartFiltering failed (error = %d)\n", status);
		goto exit;
	}

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = DelProtectCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DelProtectDeviceControl;
	DriverObject->DriverUnload = DelProtectDriverUnload;
	dirs.Init();
	DbgPrint("DelProtect Driver Loaded\n");

exit:
	if (!NT_SUCCESS(status)) {
		if (DirFilterData.FilterHandle)
			FltUnregisterFilter(DirFilterData.FilterHandle);
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

	FltUnregisterFilter(DirFilterData.FilterHandle);

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

	auto& params = Data->Iopb->Parameters.Create;
	auto returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (params.Options & FILE_DELETE_ON_CLOSE) {
		DbgPrint("Delete on close: %wZ\n", &Data->Iopb->TargetFileObject->FileName);
		if (!IsDeleteAllowed(Data)) {
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

	auto& params = Data->Iopb->Parameters.SetFileInformation;
	auto returnStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (params.FileInformationClass != FileDispositionInformation && params.FileInformationClass != FileDispositionInformationEx)
		return returnStatus;

	if (auto info = (FILE_DISPOSITION_INFORMATION*)params.InfoBuffer; info->DeleteFile) {
		DbgPrint("Delete on close: %wZ\n", &Data->Iopb->TargetFileObject->FileName);
		auto flag = false;
		if (!IsDeleteAllowed(Data)) {
			flag = true;
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			returnStatus = FLT_PREOP_COMPLETE;
			DbgPrint("Prevent delete from IRP_MJ_SET_INFORMATION\n");
		}
		if (!flag)
			DbgPrint("Not enter IsDeleteAllowed\n");
	}

	return returnStatus;
}

bool IsDeleteAllowed(_Inout_ PFLT_CALLBACK_DATA Data) {
	PFLT_FILE_NAME_INFORMATION nameInfo = nullptr;
	auto allowStatus = true;

	do {
		auto status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &nameInfo);
		if (!NT_SUCCESS(status))
			break;

		status = FltParseFileNameInformation(nameInfo);
		if (!NT_SUCCESS(status))
			break;

		UNICODE_STRING path;
		path.Length = path.MaximumLength = nameInfo->Volume.Length + nameInfo->ParentDir.Length + nameInfo->Share.Length;
		path.Buffer = nameInfo->Volume.Buffer;

		DbgPrint("Checking directory: %wZ\n", path);

		AutoLock<FastMutex> lock(dirs.DirNamesLock);
		if (dirs.FindDirectory(&path, false) >= 0) {
			allowStatus = false;
			DbgPrint("File not allowed to delete: %wZ\n", &nameInfo->Name);
		}

	} while (false);

	if (nameInfo)
		FltReleaseFileNameInformation(nameInfo);
	return allowStatus;
}

VOID
DelProtectDriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
)
{
	dirs.ClearAll();
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

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_DELPROTECT_ADD_DIR: {
		auto buffer = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
		if (!buffer) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		auto bufferLength = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (bufferLength > 1024) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		buffer[bufferLength / sizeof(WCHAR) - 1] = L'\0';
		DbgPrint("Add directory: %ws\n", buffer);

		auto dosNameLength = wcslen(buffer);
		if (dosNameLength < 3) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		AutoLock<FastMutex> lock(dirs.DirNamesLock);
		
		UNICODE_STRING strName;
		RtlInitUnicodeString(&strName, buffer);
		DbgPrint("Checking directory: %wZ\n", &strName);

		if (dirs.FindDirectory(&strName, true) >= 0) {
			status = STATUS_OBJECT_NAME_EXISTS;
			break;
		}

		if (dirs.DirNamesCount == MaxDirectorys) {
			status = STATUS_TOO_MANY_NAMES;
			break;
		}

		for (int i = 0; i < MaxDirectorys; i++) {
			DbgPrint("Start to find empty slot.\n");
			if (dirs.DirNames[i].DosName.Buffer == nullptr) {
				auto length = (dosNameLength + 2) * sizeof(WCHAR);
				auto name = (WCHAR*)ExAllocatePool2(POOL_FLAG_PAGED, length, DRIVER_TAG);
				
				if (!name) {
					status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}

				wcscpy_s(name, length / sizeof(WCHAR), buffer);

				if (buffer[dosNameLength - 1] != L'\\')
					wcscat_s(name, dosNameLength + 2, L"\\");

				status = dirs.ConvertDosNameToNtName(name, &dirs.DirNames[i].NtName);
				if (!NT_SUCCESS(status)) {
					ExFreePool(name);
					break;
				}

				RtlInitUnicodeString(&dirs.DirNames[i].DosName, name);
				DbgPrint("dirs.DirNames[%d].DosName: %wZ\n", i, &dirs.DirNames[i].DosName);
				DbgPrint("dirs.DirNames[%d].NtName: %wZ\n", i, &dirs.DirNames[i].NtName);
				dirs.DirNamesCount++;
				DbgPrint("Add: %wZ <=> %wZ\n", &dirs.DirNames[i].DosName, &dirs.DirNames[i].NtName);
				break;
			}
		}

		break;
	}
	case IOCTL_DELPROTECT_REMOVE_DIR: {
		auto buffer = (WCHAR*)Irp->AssociatedIrp.SystemBuffer;
		if (!buffer) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		auto bufferLength = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (bufferLength > 1024) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		buffer[bufferLength / sizeof(WCHAR) - 1] = L'\0';
		DbgPrint("Deleting directory: %ws...\n", buffer);

		auto dosNameLength = wcslen(buffer);
		if (dosNameLength < 3) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		if (buffer[dosNameLength - 1] != L'\\')
			wcscat_s(buffer, dosNameLength + 2, L"\\");

		AutoLock<FastMutex> lock(dirs.DirNamesLock);

		UNICODE_STRING strName;
		RtlInitUnicodeString(&strName, buffer);
		DbgPrint("Checking directory: %wZ\n", &strName);



		if (auto index = dirs.FindDirectory(&strName, true); index >= 0) {
			dirs.DirNames[index].Free();
			dirs.DirNamesCount--;
			DbgPrint("Delete: %wZ <=> %wZ\n", &dirs.DirNames[index].DosName, &dirs.DirNames[index].NtName);
		}
		else {
			DbgPrint("Not found %wZ\n", &strName);
			status = STATUS_NOT_FOUND;
		}

		break;
	}
	case IOCTL_DELPROTECT_CLEAR_DIR: {
		dirs.ClearAll();
		break;
	}


	default: {
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	}

	return CompleteRequestAux(Irp, status, 0);
}