#pragma once

#include "FastMutex.h"

#define PROCESS_TERMINATE 1

const int maxPids = 256;

struct Globals {
	void Init();
	bool AddProcess(ULONG pid);
	bool RemoveProcess(ULONG pid);
	bool FindProcess(ULONG pid);

public:
	ULONG PidsCount;
	ULONG Pids[maxPids];
	PVOID RegHandle;
	FastMutex Lock;
};

void Globals::Init() {
	PidsCount = 0;
	memset(Pids, 0, sizeof(Pids));
	Lock.Init();
	RegHandle = nullptr;
}

bool Globals::FindProcess(ULONG pid) {
	for (ULONG i = 0; i < maxPids; i++) {
		if (Pids[i] == pid) {
			return true;
		}
	}
	return false;
}

bool Globals::RemoveProcess(ULONG pid) {
	for (ULONG i = 0; i < maxPids; i++) {
		if (Pids[i] == pid) {
			Pids[i] = 0;
			PidsCount--;
			return true;
		}
	}
	return false;
}

bool Globals::AddProcess(ULONG pid) {
	for (ULONG i = 0; i < maxPids; i++) {
		if (Pids[i] == 0) {
			Pids[i] = pid;
			PidsCount++;
			return true;
		}
	}
	return false;
}