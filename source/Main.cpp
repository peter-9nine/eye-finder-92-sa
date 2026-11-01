#include "pch.h"
#include "BrowserOverlay.h"
#include "MailSystem.h"
#include "PadInput.h"
#include "Paths.h"

using namespace plugin;

namespace {

BrowserOverlay gOverlay;
MailSystem gMail;
PadInput gPad;
safetyhook::InlineHook gMouseSetPos;

// Hooks the call, not the function, to chain whatever was there if another mod already changed it.
// SDK events are built on this, but they don't provide the return value, which is needed here.
using GenericSaveCall = injector::function_hooker<injector::scoped_call, 0x00619081, char(int)>;
using GenericLoadCall = injector::function_hooker<injector::scoped_call, 0x0053C70B, char(bool*)>;

// The game recenters the cursor every frame, which must be stopped while the browser is open.
void __cdecl HookMouseSetPos(RwV2d* position) {
	if (gOverlay.UsesCursor()) return;
	gMouseSetPos.original<int(__cdecl*)(RwV2d*)>()(position);
}

}  // namespace

struct Main {
	Main() {
		Log("EyeFinder92SA loaded");
		gMouseSetPos = safetyhook::create_inline(reinterpret_cast<void*>(0x007453F0), HookMouseSetPos);

		// Each saved game has its own mod file. It's only written if the game really saved.
		injector::make_static_hook<GenericSaveCall>([](GenericSaveCall::func_type original, int unused) {
			const char saved = original(unused);
			if (saved) gMail.OnGameSaved();
			return saved;
		});
		injector::make_static_hook<GenericLoadCall>([](GenericLoadCall::func_type original, bool* notFound) {
			const char loaded = original(notFound);
			if (loaded) gMail.OnGameLoaded();
			return loaded;
		});

		Events::initRwEvent += [] {
			gPad.Initialize();
			gOverlay.SetPad(gPad);
			gOverlay.InstallWindowHook();
			gMail.Initialize(gOverlay, gPad);
		};
		Events::gameProcessEvent += [] {
			gPad.Update();
			gOverlay.Update();
			gMail.Update();
		};
		// The pager goes after, so that it also shows over the browser.
		Events::drawAfterFadeEvent += [] {
			gOverlay.Draw();
			gMail.Draw();
		};
		// Fires on starting and before loading: if it's a load, OnGameLoaded brings the mail afterwards.
		Events::restartGameEvent += [] {
			gOverlay.Restart();
			gMail.OnNewGame();
		};
		Events::d3dLostEvent += [] { gOverlay.OnDeviceLost(); };
		Events::d3dResetEvent += [] { gOverlay.OnDeviceReset(); };
		Events::shutdownRwEvent += [] {
			gOverlay.Shutdown();
			gPad.Shutdown();
		};
	}
} gInstance;
