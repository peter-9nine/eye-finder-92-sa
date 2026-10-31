#include "Paths.h"

#include <windows.h>

#include <fstream>
#include <mutex>

std::wstring PluginDirectory() {
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&PluginDirectory), &module);

	wchar_t buffer[MAX_PATH]{};
	GetModuleFileNameW(module, buffer, MAX_PATH);

	std::wstring path(buffer);
	return path.substr(0, path.find_last_of(L"\\/"));
}

std::wstring FileUrl(const std::wstring& path) {
	std::wstring url = L"file:///";
	for (const wchar_t c : path) {
		if (c == L'\\') url += L'/';
		else if (c == L' ') url += L"%20";
		else url += c;
	}
	return url;
}

void Log(const std::string& line) {
	static std::mutex mutex;
	std::lock_guard lock(mutex);

	SYSTEMTIME time;
	GetLocalTime(&time);
	char stamp[32];
	sprintf_s(stamp, "%02d:%02d:%02d ", time.wHour, time.wMinute, time.wSecond);

	std::ofstream file(PluginDirectory() + L"\\EyeFinder92SA.log", std::ios::app);
	file << stamp << line << '\n';
}
