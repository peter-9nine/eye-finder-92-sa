// A small mod that uses EyeFinder92SA's public API, for testing and as an example.
//
//   F9        email, announced in the help box
//   F10       email, announced on the pager
//   F11       pager message, no email
//   Shift+F11 help box text, no email
#include <windows.h>

#include <cstdio>
#include <string>

#include "EyeFinderApi.h"

namespace {

HMODULE gModule = nullptr;

void Log(const std::string& line) {
	char path[MAX_PATH]{};
	GetModuleFileNameA(gModule, path, MAX_PATH);
	std::string file = path;
	file = file.substr(0, file.find_last_of("\\/")) + "\\EyeFinderApiDemo.log";

	if (FILE* log = fopen(file.c_str(), "a")) {
		fprintf(log, "%s\n", line.c_str());
		fclose(log);
	}
}

bool Pressed(int key, bool& wasDown) {
	const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
	const bool pressed = down && !wasDown;
	wasDown = down;
	return pressed;
}

bool GameHasFocus() {
	DWORD process = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &process);
	return process == GetCurrentProcessId();
}

DWORD WINAPI Run(void*) {
	// EyeFinder92SA may load after this mod, so give it time before looking for it.
	Sleep(10000);
	Log("EyeFinder92SA installed: " + std::string(eyefinder::IsInstalled() ? "yes" : "no") +
		", API version " + std::to_string(eyefinder::GetApiVersion()));

	int emails = 0;
	bool f9 = false, f10 = false, f11 = false;

	for (;;) {
		Sleep(30);
		if (!GameHasFocus()) continue;

		if (Pressed(VK_F9, f9)) {
			const std::string subject = "Test email " + std::to_string(++emails);
			eyefinder::SendEmail("Sweet", subject.c_str(),
				"Yo CJ, this came from another mod through the EyeFinder API.\n\n"
				"Grove Street, home. Check https://www.eyefinder.info/ when you get a minute.",
				EF_NOTIFY_TEXT);
		}

		if (Pressed(VK_F10, f10)) {
			const std::string subject = "Test email " + std::to_string(++emails);
			eyefinder::SendEmail("Big Smoke", subject.c_str(),
				"I'll have two number 9s, a number 9 large, a number 6 with extra dip, "
				"a number 7, two number 45s, one with cheese, and a large soda.",
				EF_NOTIFY_PAGER);
		}

		if (Pressed(VK_F11, f11)) {
			if (GetAsyncKeyState(VK_SHIFT) & 0x8000) eyefinder::Notify("This text came from another mod.");
			else eyefinder::Page("Cesar", "Meet me at the garage in Little Mexico. Bring the car and come alone, ese.");
		}
	}
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		gModule = instance;
		DisableThreadLibraryCalls(instance);
		if (HANDLE thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr)) CloseHandle(thread);
	}
	return TRUE;
}
