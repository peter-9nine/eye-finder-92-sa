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

std::wstring SaveDataDirectory() {
	return PluginDirectory() + L"\\save data";
}

// La partida en curso. Solo pasa a un archivo de partida cuando el jugador guarda.
std::wstring SessionSavePath() {
	return SaveDataDirectory() + L"\\current session.json";
}

std::wstring FileUrl(const std::wstring& path) {
	// UTF-8 con escapes: la misma forma en que Chromium devuelve la URL, para poder compararla.
	const int size = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string utf8(size > 0 ? size - 1 : 0, '\0');
	if (size > 1) WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, utf8.data(), size, nullptr, nullptr);

	std::wstring url = L"file:///";
	wchar_t escaped[4];
	for (const char c : utf8) {
		const auto byte = static_cast<unsigned char>(c);
		if (byte == '\\') {
			url += L'/';
		}
		else if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
			byte == '-' || byte == '_' || byte == '.' || byte == '~' || byte == '/' || byte == ':') {
			url += static_cast<wchar_t>(byte);
		}
		else {
			swprintf_s(escaped, L"%%%02X", byte);
			url += escaped;
		}
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
