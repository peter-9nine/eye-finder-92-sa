// A small mod that uses EyeFinder92SA's public API, for testing and as an example.
//
//   F9        email with two answers; the good one starts a CLEO script
//   F10       email, announced on the pager
//   F11       pager message, no email
//   Shift+F11 help box text, no email
#include <windows.h>

#include <cstdio>
#include <string>

#include "EyeFinderApi.h"

namespace {

HMODULE gModule = nullptr;

std::string ModuleDirectory() {
	char path[MAX_PATH]{};
	GetModuleFileNameA(gModule, path, MAX_PATH);
	const std::string file = path;
	return file.substr(0, file.find_last_of("\\/"));
}

void Log(const std::string& line) {
	const std::string file = ModuleDirectory() + "\\EyeFinderApiDemo.log";

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

std::string DemoScriptPath() {
	return ModuleDirectory() + "\\browser\\sites\\www.eyefinder.info\\scripts\\hello.efs";
}

// Runs only if the test marker file exists: starts the demo script and checks that it gave CJ money.
void SelfTest() {
	while (eyefinder::GetUnreadCount() == 0) Sleep(200);  // A game is running once the inbox has mail.
	Sleep(3000);

	const auto money = reinterpret_cast<volatile int*>(0xB7CE50);
	const int before = *money;
	const int accepted = eyefinder::RunCleoScript(DemoScriptPath().c_str());
	Sleep(3000);
	Log("Self test: accepted " + std::to_string(accepted) + ", money " + std::to_string(before) + " -> " + std::to_string(*money));

	eyefinder::SendEmailWithReplies("Test", "Self test", "Body", EF_NOTIFY_NONE, "apidemo_selftest", "Good", "Bad");
	Sleep(1000);
	Log("Self test: reply state " + std::to_string(eyefinder::GetReply("apidemo_selftest")));

	// A real CLEO script reads a value and writes two back. See values_test.txt.
	eyefinder::SetValueInt("test.in", 41);
	eyefinder::RunCleoScript((ModuleDirectory() + "\\values_test.efs").c_str());
	Sleep(2000);
	char text[64];
	const int found = eyefinder::GetValue("test.text", text, sizeof(text));
	Log("Self test: CLEO read and wrote " + std::to_string(eyefinder::GetValueInt("test.out", -99)) + ", text " + (found ? text : "(none)"));

	// The plugin's log should show this help box text only once.
	eyefinder::SendEmail("Test", "Notification test", "Body", EF_NOTIFY_TEXT);
}

DWORD WINAPI Run(void*) {
	// EyeFinder92SA may load after this mod, so give it time before looking for it.
	Sleep(10000);
	Log("EyeFinder92SA installed: " + std::string(eyefinder::IsInstalled() ? "yes" : "no") +
		", API version " + std::to_string(eyefinder::GetApiVersion()));

	int emails = 0;
	bool f9 = false, f10 = false, f11 = false;
	std::string lastKey;
	int lastAnswer = EF_REPLY_NONE;

	if (GetFileAttributesA((ModuleDirectory() + "\\EyeFinderApiDemo.selftest").c_str()) != INVALID_FILE_ATTRIBUTES) SelfTest();

	for (;;) {
		Sleep(30);
		const int answer = lastKey.empty() ? EF_REPLY_NONE : eyefinder::GetReply(lastKey.c_str());
		if (answer != lastAnswer) {
			lastAnswer = answer;
			Log("The player answered " + lastKey + ": " + (answer == EF_REPLY_GOOD ? "good" : "bad"));
		}

		if (!GameHasFocus()) continue;

		if (Pressed(VK_F9, f9)) {
			// A new key each time, because each key is only delivered once per saved game.
			lastKey = "apidemo_" + std::to_string(GetTickCount());
			const std::string subject = "Test email " + std::to_string(++emails);
			eyefinder::SendEmailWithReplies("Sweet", subject.c_str(),
				"Yo CJ, this came from another mod through the EyeFinder API.\n\n"
				"You coming back to Grove Street or what?",
				EF_NOTIFY_TEXT, lastKey.c_str(),
				"Of course, Grove Street 4 life!", "Nah, I'm busy making money.",
				DemoScriptPath().c_str());
			lastAnswer = EF_REPLY_NONE;
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
