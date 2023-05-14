#include <Windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <stdlib.h>

#include "../../DelProtect/DelProtect/Common.h"

typedef enum class _OPTIONS {
	UNKNOWN,
	ADD,
	REMOVE,
	CLEAR
} OPTIONS;

int main(int argc, char* argv[]) {

	if (argc < 2) {
		printf("Usage: %s [add] | [remove] | [clear] <directory name>\n", argv[0]);
		return -1;
	}

	OPTIONS op;

	printf("%s %s\n", argv[1], argv[2]);

	if (!strcmp(argv[1], "add")) {
		op = OPTIONS::ADD;
	}
	else if (!strcmp(argv[1], "remove")) {
		op = OPTIONS::REMOVE;
	}
	else if (!strcmp(argv[1], "clear")) {
		op = OPTIONS::CLEAR;
	}
	else {
		printf("[-] Unknown option\n");
		return -1;
	}

	auto hFile = CreateFile(L"\\\\.\\DelProtect", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		printf("[-] Failed to open DelProtect device (error = %d)\n", GetLastError());
		return -1;
	}

	bool success = false;
	DWORD returned;
	wchar_t* fileName = (wchar_t*)malloc(sizeof(wchar_t) * (strlen(argv[2]) + 1));
	swprintf_s(fileName, strlen(argv[2]) + 1, L"%S", argv[2]);

	switch (op) {
	case OPTIONS::ADD: {
		success = DeviceIoControl(hFile, IOCTL_DELPROTECT_ADD_DIR, (PVOID)fileName,
			static_cast<DWORD>(wcslen(fileName) + 1) * sizeof(WCHAR), nullptr, 0,
			&returned, nullptr);
		break;
	}
	case OPTIONS::REMOVE: {
		success = DeviceIoControl(hFile, IOCTL_DELPROTECT_REMOVE_DIR, (PVOID)fileName,
			static_cast<DWORD>(wcslen(fileName) + 1) * sizeof(WCHAR), nullptr, 0,
			&returned, nullptr);
		break;
	}
	case OPTIONS::CLEAR: {
		success = DeviceIoControl(hFile, IOCTL_DELPROTECT_CLEAR_DIR, nullptr,
			0, nullptr, 0,
			&returned, nullptr);
		break;
	}

	default:
		break;
	}

	if (!success) {
		printf("[-] Failed to send IOCTL to DelProtect device\n");
		return -1;
	}

	CloseHandle(hFile);

	return 0;
}