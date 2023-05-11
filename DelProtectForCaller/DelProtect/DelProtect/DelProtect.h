#pragma once

#include "FastMutex.h"

const int MaxExecutables = 32;

struct Executables {
public:
	int ExeNamesCount;
	WCHAR* ExeNames[MaxExecutables];
	FastMutex ExeNamesLock;

public:
	bool FindExecutable(PCWSTR name);
	void ClearAll();
	void Init();
};

bool Executables::FindExecutable(PCWSTR name) {
	AutoLock<FastMutex> lock(ExeNamesLock);
	DbgPrint("FindExecutable: %S\n", name);
	if (ExeNamesCount == 0)
		return false;

	for (int i = 0; i < MaxExecutables; i++) {
		if (ExeNames[i] && !_wcsicmp(ExeNames[i], name)) {
			DbgPrint("FindExecutable: found\n");
			return true;
		}
	}
	
	return false;
}

void Executables::ClearAll() {
	AutoLock<FastMutex> lock(ExeNamesLock);
	for (int i = 0; i < MaxExecutables; i++) {
		if (ExeNames[i]) {
			ExFreePool(ExeNames[i]);
			ExeNames[i] = nullptr;
		}
	}
	ExeNamesCount = 0;
}

void Executables::Init() {
	ExeNamesLock.Init();
	ExeNamesCount = 0;
	RtlZeroMemory(ExeNames, sizeof(ExeNames));
}