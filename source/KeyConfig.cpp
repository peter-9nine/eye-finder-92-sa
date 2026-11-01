#include "KeyConfig.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace {

constexpr char kDefaultFile[] =
	"; EyeFinder92SA keys. Restart the game after changing them.\r\n"
	";\r\n"
	"; You can use: F1 to F12, A to Z, 0 to 9, Numpad0 to Numpad9, Up, Down, Left, Right,\r\n"
	"; PageUp, PageDown, Home, End, Insert, Delete, Space, Backspace, Pause.\r\n"
	"; Use None to turn a key off. A letter or number won't open the pager while the computer\r\n"
	"; is open, so that you can still type.\r\n"
	"\r\n"
	"[Keys]\r\n"
	"; For testing: open the browser anywhere, or close it. Add Shift for a police session, or Ctrl for\r\n"
	"; a dark web session. Players use TAB or L1 at a computer, so set this to None to turn it off.\r\n"
	"Browser=F8\r\n"
	"\r\n"
	"; Bring up the pager with the latest message, or put it away.\r\n"
	"PagerOpen=F6\r\n"
	"\r\n"
	"; While the pager is up, go to older and newer messages.\r\n"
	"PagerOlder=PageUp\r\n"
	"PagerNewer=PageDown\r\n";

std::string ToLower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return text;
}

std::string Trim(const std::string& text) {
	const size_t first = text.find_first_not_of(" \t");
	if (first == std::string::npos) return {};
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

void ReadKey(const std::wstring& path, const wchar_t* name, unsigned int& key, std::string& problems) {
	wchar_t buffer[64]{};
	GetPrivateProfileStringW(L"Keys", name, L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());

	char narrow[64]{};
	WideCharToMultiByte(CP_UTF8, 0, buffer, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
	const std::string text = Trim(narrow);
	if (text.empty()) return;

	const int parsed = ParseKeyName(text);
	if (parsed < 0) {
		char wideName[64]{};
		WideCharToMultiByte(CP_UTF8, 0, name, -1, wideName, sizeof(wideName) - 1, nullptr, nullptr);
		problems += std::string("Unknown key '") + text + "' for " + wideName + ": using the default. ";
		return;
	}
	key = static_cast<unsigned int>(parsed);
}

}  // namespace

int ParseKeyName(const std::string& name) {
	const std::string key = ToLower(Trim(name));
	if (key.empty()) return -1;
	if (key == "none") return 0;

	if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') return 'A' + (key[0] - 'a');
	if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') return key[0];

	if (key[0] == 'f' && key.size() <= 3) {
		const int number = std::atoi(key.c_str() + 1);
		if (number >= 1 && number <= 12) return VK_F1 + number - 1;
	}

	if (key.rfind("numpad", 0) == 0 && key.size() == 7 && key[6] >= '0' && key[6] <= '9') return VK_NUMPAD0 + (key[6] - '0');

	static const struct { const char* name; int key; } kNames[] = {
		{ "up", VK_UP }, { "down", VK_DOWN }, { "left", VK_LEFT }, { "right", VK_RIGHT },
		{ "pageup", VK_PRIOR }, { "pagedown", VK_NEXT }, { "home", VK_HOME }, { "end", VK_END },
		{ "insert", VK_INSERT }, { "delete", VK_DELETE }, { "space", VK_SPACE },
		{ "backspace", VK_BACK }, { "pause", VK_PAUSE },
	};
	for (const auto& entry : kNames) {
		if (key == entry.name) return entry.key;
	}
	return -1;
}

KeyConfig LoadKeyConfig(const std::wstring& path, std::string& problems) {
	KeyConfig config;

	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		std::ofstream file(path.c_str(), std::ios::binary);
		file << kDefaultFile;
		return config;
	}

	ReadKey(path, L"Browser", config.browser, problems);
	ReadKey(path, L"PagerOpen", config.pagerOpen, problems);
	ReadKey(path, L"PagerOlder", config.pagerOlder, problems);
	ReadKey(path, L"PagerNewer", config.pagerNewer, problems);

	const unsigned int keys[] = { config.browser, config.pagerOpen, config.pagerOlder, config.pagerNewer };
	for (size_t i = 0; i < std::size(keys); ++i) {
		for (size_t j = i + 1; j < std::size(keys); ++j) {
			if (keys[i] != 0 && keys[i] == keys[j]) {
				problems += "Two actions use the same key: check EyeFinder92SA.ini. ";
				return config;
			}
		}
	}
	return config;
}
