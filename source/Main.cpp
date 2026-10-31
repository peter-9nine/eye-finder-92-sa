#include "pch.h"
#include "BrowserOverlay.h"
#include "MailSystem.h"
#include "Paths.h"

using namespace plugin;

namespace {

BrowserOverlay gOverlay;
MailSystem gMail;
safetyhook::InlineHook gMouseSetPos;
safetyhook::InlineHook gGenericSave;
safetyhook::InlineHook gGenericLoad;

// El juego recentra el cursor en cada fotograma; con el navegador abierto hay que impedirlo.
void __cdecl HookMouseSetPos(RwV2d* position) {
	if (gOverlay.UsesCursor()) return;
	gMouseSetPos.original<int(__cdecl*)(RwV2d*)>()(position);
}

// Cada partida guardada tiene su archivo del mod. Solo se escribe si el juego ha guardado de verdad.
char __cdecl HookGenericSave(int unused) {
	const char saved = gGenericSave.original<char(__cdecl*)(int)>()(unused);
	if (saved) gMail.OnGameSaved();
	return saved;
}

char __cdecl HookGenericLoad(bool* notFound) {
	const char loaded = gGenericLoad.original<char(__cdecl*)(bool*)>()(notFound);
	if (loaded) gMail.OnGameLoaded();
	return loaded;
}

}  // namespace

struct Main {
	Main() {
		Log("EyeFinder92SA loaded");
		gMouseSetPos = safetyhook::create_inline(reinterpret_cast<void*>(0x007453F0), HookMouseSetPos);
		gGenericSave = safetyhook::create_inline(reinterpret_cast<void*>(0x005D13E0), HookGenericSave);
		gGenericLoad = safetyhook::create_inline(reinterpret_cast<void*>(0x005D17B0), HookGenericLoad);

		Events::initRwEvent += [] {
			gOverlay.InstallWindowHook();
			gMail.Initialize(gOverlay);
		};
		Events::gameProcessEvent += [] {
			gOverlay.Update();
			gMail.Update();
		};
		// El busca va después, para verse también sobre el navegador.
		Events::drawAfterFadeEvent += [] {
			gOverlay.Draw();
			gMail.Draw();
		};
		// Se dispara al empezar y antes de cargar: si es una carga, OnGameLoaded trae luego los correos.
		Events::restartGameEvent += [] {
			gOverlay.Restart();
			gMail.OnNewGame();
		};
		Events::d3dLostEvent += [] { gOverlay.OnDeviceLost(); };
		Events::d3dResetEvent += [] { gOverlay.OnDeviceReset(); };
		Events::shutdownRwEvent += [] { gOverlay.Shutdown(); };
	}
} gInstance;
