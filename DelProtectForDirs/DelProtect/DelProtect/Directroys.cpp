#include "DelProtect.h"
#include "KString.h"

void DirectoryName::Free() {
	if (DosName.Buffer) {
		ExFreePool(DosName.Buffer);
		DosName.Buffer = nullptr;
	}
	if (NtName.Buffer) {
		ExFreePool(NtName.Buffer);
		NtName.Buffer = nullptr;
	}
}


int Directorys::FindDirectory(PUNICODE_STRING name, bool dosName) {
	if (DirNamesCount == 0)
		return -1;

	for (int i = 0; i < DirNamesCount; i++) {
		const auto& dir = dosName ? DirNames[i].DosName : DirNames[i].NtName;
		if (dir.Buffer && RtlEqualUnicodeString(name, &dir, TRUE))
			return i;
	}

	return -1;
}

void Directorys::ClearAll() {
	AutoLock<FastMutex> lock(DirNamesLock);
	for (int i = 0; i < DirNamesCount; i++) {
		if (DirNames[i].DosName.Buffer) {
			ExFreePool(DirNames[i].DosName.Buffer);
			DirNames[i].DosName.Buffer = nullptr;
		}
		if (DirNames[i].NtName.Buffer) {
			ExFreePool(DirNames[i].NtName.Buffer);
			DirNames[i].NtName.Buffer = nullptr;
		}
	}
	DirNamesCount = 0;
}

void Directorys::Init() {
	DirNamesCount = 0;
	DirNamesLock.Init();
}

NTSTATUS Directorys::ConvertDosNameToNtName(PCWSTR dosName, PUNICODE_STRING ntName) {
	ntName->Buffer = nullptr;
	auto dosNameLength = wcslen(dosName);

	if (dosNameLength < 3)
		return STATUS_BUFFER_TOO_SMALL;

	if (dosName[2] != L'\\' || dosName[1] != L':')
		return STATUS_INVALID_PARAMETER;

	kstring symLink(L"\\??\\", POOL_FLAG_PAGED, DRIVER_TAG);
	UNICODE_STRING symLinkFull;

	symLink.Append(dosName, 2);
	symLink.GetUnicodeString(&symLinkFull);
	OBJECT_ATTRIBUTES symLinkAttr;
	InitializeObjectAttributes(&symLinkAttr, &symLinkFull, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, nullptr, nullptr);

	HANDLE hSymLink = nullptr;
	auto status = STATUS_SUCCESS;

	status = ZwOpenSymbolicLinkObject(&hSymLink, GENERIC_READ, &symLinkAttr);
	if (!NT_SUCCESS(status))
		goto exit;

	USHORT maxLength = 1024;
	
	ntName->Buffer = (WCHAR*)ExAllocatePool2(POOL_FLAG_PAGED, maxLength, DRIVER_TAG);
	if (!ntName->Buffer) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	ntName->MaximumLength = maxLength;
	
	status = ZwQuerySymbolicLinkObject(hSymLink, ntName, nullptr);
	if (!NT_SUCCESS(status)) {
		if (ntName->Buffer) {
			ExFreePool(ntName->Buffer);
			ntName->Buffer = nullptr;
		}
	}
	else {
		RtlAppendUnicodeToString(ntName, dosName + 2);
	}

exit:
	if (hSymLink)
		ZwClose(hSymLink);

	return status;
}