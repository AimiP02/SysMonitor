#include <Windows.h>
#include <iostream>
#include <cstdio>
#include <vector>

#include "../../ProcessProtect/ProcessProtect/ProcProtectCommon.h"

enum class Options {
	UNKNOWN,
	ADD,
	REMOVE,
	CLEAR
};

int Error(const char* msg) {
	printf("%s (Error: %u)\n", msg, GetLastError());
	return 1;
}

std::vector<DWORD> ParsePid(char* buffer[], int count) {
	std::vector<DWORD> pids;
	for (int i = 0; i < count; i++) {
		pids.push_back(atoi(buffer[i]));
	}
	return pids;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("Usage: %s [add] | [remove] | [clear] <pid> <pid> ...\n", argv[0]);
		return 1;
	}
	
	Options option;

	if (strcmp(argv[1], "add") == 0) {
		option = Options::ADD;
	}
	else if (strcmp(argv[1], "remove") == 0) {
		option = Options::REMOVE;
	}
	else if (strcmp(argv[1], "clear") == 0) {
		option = Options::CLEAR;
	}
	else {
		option = Options::UNKNOWN;
	}

	if (option == Options::UNKNOWN) {
		printf("Unknown option: %s\n", argv[1]);
		return 1;
	}

	HANDLE hFile = CreateFile(L"\\\\.\\ProcProtect", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		return Error("Failed to open device");
	}

	std::vector<DWORD> pids = ParsePid(argv + 2, argc - 2);

	DWORD bytesReturned;
	bool status = false;

	switch (option) {
	case Options::ADD: {
		status = DeviceIoControl(hFile, IOCTL_PROCESS_PROTECT_BY_PID, pids.data(), static_cast<DWORD>(pids.size()) * sizeof(DWORD), 
									nullptr, 0, &bytesReturned, nullptr);
		for (auto&& i : pids) {
			printf("Pid: %d is protected.\n", i);
		}
		break;
	}
	case Options::REMOVE: {
		status = DeviceIoControl(hFile, IOCTL_PROCESS_UNPROTECT_BY_PID, pids.data(), static_cast<DWORD>(pids.size()) * sizeof(DWORD),
												nullptr, 0, &bytesReturned, nullptr);
		for (auto&& i : pids) {
			printf("Pid: %d is unprotected.\n", i);
		}
		break;
	}
	case Options::CLEAR: {
		status = DeviceIoControl(hFile, IOCTL_PROCESS_PROTECT_CLEAR, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
		printf("Protected process cleared.\n");
		break;
	}

	default:
		break;
	}

	if (!status) {
		return Error("Failed to send IOCTL");
	}

	CloseHandle(hFile);

	return 0;
}