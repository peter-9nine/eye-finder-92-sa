#include "pch.h"
#include "BrowserOverlay.h"
#include "Paths.h"

using namespace plugin;

namespace {

BrowserOverlay gOverlay;
safetyhook::InlineHook gMouseSetPos;

// El juego recentra el cursor en cada fotograma; con el navegador abierto hay que impedirlo.
void __cdecl HookMouseSetPos(RwV2d* position) {
	if (gOverlay.UsesCursor()) return;
	gMouseSetPos.original<int(__cdecl*)(RwV2d*)>()(position);
}

}  // namespace

struct Main {
	Main() {
		Log("EyeFinder92SA loaded");
		gMouseSetPos = safetyhook::create_inline(reinterpret_cast<void*>(0x007453F0), HookMouseSetPos);

		Events::initRwEvent += [] { gOverlay.InstallWindowHook(); };
		Events::gameProcessEvent += [] { gOverlay.Update(); };
		Events::drawAfterFadeEvent += [] { gOverlay.Draw(); };
		Events::restartGameEvent += [] { gOverlay.Restart(); };
		Events::d3dLostEvent += [] { gOverlay.OnDeviceLost(); };
		Events::d3dResetEvent += [] { gOverlay.OnDeviceReset(); };
		Events::shutdownRwEvent += [] { gOverlay.Shutdown(); };
	}
} gInstance;
