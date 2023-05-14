#pragma once

#include "FastMutex.h"

const int MaxDirectorys = 32;

#define DRIVER_TAG 'ledp'

struct DirectoryName {
public:
	UNICODE_STRING DosName;
	UNICODE_STRING NtName;

public:
	void Free();
};

struct Directorys : DirectoryName {
public:
	int DirNamesCount;
	DirectoryName DirNames[MaxDirectorys];
	FastMutex DirNamesLock;

public:
	int FindDirectory(PUNICODE_STRING name, bool dosName);
	void ClearAll();
	void Init();
	NTSTATUS ConvertDosNameToNtName(PCWSTR dosName, PUNICODE_STRING ntName);
};