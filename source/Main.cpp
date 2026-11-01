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

// Se engancha la llamada y no la función, para encadenar lo que hubiera si otro mod ya la cambió.
// Es lo que usan por dentro los eventos del SDK, pero estos no dan el valor devuelto, y hace falta.
using GenericSaveCall = injector::function_hooker<injector::scoped_call, 0x00619081, char(int)>;
using GenericLoadCall = injector::function_hooker<injector::scoped_call, 0x0053C70B, char(bool*)>;

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

		// Cada partida guardada tiene su archivo del mod. Solo se escribe si el juego ha guardado de verdad.
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
		Events::shutdownRwEvent += [] {
			gOverlay.Shutdown();
			gPad.Shutdown();
		};
	}
} gInstance;
