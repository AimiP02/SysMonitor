#include <Windows.h>
#include <cstdio>
#include <iostream>

#include "../../sysmon/sysmon/SysmonCommon.h"

BYTE buffer[1 << 16];

int Error(const char* msg) {
	printf("%s failed. error code = (%d)\n", msg, GetLastError());
	return 1;
}

void DisplayTime(const LARGE_INTEGER& time) {
	SYSTEMTIME st;
	FileTimeToSystemTime((FILETIME*)&time, &st);
	printf("%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayInfo(BYTE* buffer, DWORD size) {
	auto count = size;
	while (count > 0) {
		auto header = (ItemHeader*)buffer;

		switch (header->Type) {
		case ItemType::ProcessExit: {
			DisplayTime(header->Time);
			auto info = (ProcessExitInfo*)buffer;
			printf("Process %d Exited\n", info->ProcessId);
			break;
		}
		case ItemType::ProcessCreate: {
			DisplayTime(header->Time);
			auto info = (ProcessCreateInfo*)buffer;
			std::wstring commandLine((wchar_t*)(buffer + info->CommandLineOffset), info->CommandLineLength);
			printf("Process %d Created: %ws\n", info->ProcessId, commandLine.c_str());
			break;
		}
		default:
			break;
		}
		buffer += header->Size;
		count -= header->Size;
	}
}

int main() {
	auto hDevice = CreateFile(L"\\\\.\\Sysmon", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE) {
		return Error("CreateFile");
	}

	while (true) {
		DWORD bytes = 0;
		if (!ReadFile(hDevice, buffer, sizeof(buffer), &bytes, nullptr)) {
			return Error("ReadFile");
		}
		if (bytes != 0) {
			DisplayInfo(buffer, bytes);
		}

		Sleep(200);
	}
	return 0;
}