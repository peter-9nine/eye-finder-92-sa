#include "pch.h"
#include "MailSystem.h"
#include "ApiQueue.h"
#include "EyeFinderApi.h"
#include "Paths.h"

namespace {

// Los avisos no deben interrumpir escenas ni aparecer sobre el menú.
bool GameCanShowNotifications() {
	return !FrontEndMenuManager.m_bMenuActive && !TheCamera.m_bWideScreenOn && !CCutsceneMgr::ms_running;
}

// El juego copia el texto aquí solo si lo acepta. No sirve HelpMessageDisplayed: sigue en falso
// hasta que el cuadro se dibuja, y por eso el aviso se repetía sin fin.
bool HelpMessageAccepted(const std::string& text) {
	return std::strncmp(CHud::m_pHelpMessage, text.c_str(), 399) == 0;
}

std::string GameDate() {
	const int day = static_cast<int>(CStats::GetStatValue(STAT_DAYS_PASSED_IN_GAME));
	char text[32];
	snprintf(text, sizeof(text), "Day %d, %02d:%02d", day, CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes);
	return text;
}

}  // namespace

void MailSystem::Initialize(BrowserOverlay& overlay) {
	overlay_ = &overlay;
	saveFolder_ = SaveDataDirectory();
	store_.SetSessionPath(SessionSavePath());

	std::string problems;
	keys_ = LoadKeyConfig(PluginDirectory() + L"\\EyeFinder92SA.ini", problems);
	if (!problems.empty()) Log(problems);
	overlay.SetToggleKey(keys_.browser);
	overlay.SetKeyFilter([this](unsigned int key, bool repeat) { return HandleKey(key, repeat); });

	OnNewGame();
}

void MailSystem::PublishUnreadCount() {
	const int unread = store_.UnreadCount();
	api::SetUnreadCount(unread);
	if (overlay_) overlay_->SetUnreadMail(unread);
}

void MailSystem::OnNewGame() {
	store_.Clear();
	needsWelcome_ = true;
	newMailToAnnounce_ = 0;
	texts_.clear();
	pages_.clear();
	memoryOpen_ = false;
	memoryIndex_ = 0;
	store_.FlushSession();
	api::ClearReplies();
	api::ReplaceValues({});
	stateSampleLogged_ = false;
	stateSampleTime_ = GetTickCount() + 3000;
	PublishUnreadCount();
}

void MailSystem::SyncReplies() {
	api::ClearReplies();
	for (const auto& [key, answer] : store_.Replies()) api::SetReply(key, answer);
}

std::filesystem::path MailSystem::SaveFileFor(const char* gameSaveFile) const {
	// "...\GTASAsf1.b" -> "save data\GTASAsf1.json"
	std::filesystem::path name = std::filesystem::path(gameSaveFile).filename();
	name.replace_extension(L".json");
	return saveFolder_ / name;
}

void MailSystem::OnGameSaved() {
	const char* file = CGenericGameStorage::ms_SaveFileNameJustSaved;
	if (!file || !file[0]) {
		Log("Game saved, but its file name is unknown: the mod's data was not saved");
		return;
	}

	const std::filesystem::path path = SaveFileFor(file);
	Log(std::string(store_.SaveTo(path) ? "Saved mod data to " : "Could not save mod data to ") + path.filename().string());
}

void MailSystem::OnGameLoaded() {
	const char* file = CGenericGameStorage::ms_LoadFileName;
	if (!file || !file[0]) {
		Log("Game loaded, but its file name is unknown: starting with an empty inbox");
		return;
	}

	const std::filesystem::path path = SaveFileFor(file);
	if (store_.LoadFrom(path)) {
		needsWelcome_ = false;
		Log("Loaded mod data from " + path.filename().string());
	}
	else {
		// Una partida de antes de instalar el mod, o un archivo dañado.
		Log("No mod data for " + path.filename().string() + ": starting with an empty inbox");
	}

	store_.FlushSession();
	SyncReplies();
	api::ReplaceValues(store_.Values());
	PublishUnreadCount();
}

void MailSystem::RunCleoScript(const std::string& path) {
	// CLEO puede no estar instalado: se busca en cada uso y nunca se enlaza.
	using CreateCustomScript = void*(__stdcall*)(void* fromThread, const char* filePath, int label);
	const HMODULE cleo = GetModuleHandleA("CLEO.asi");
	const auto create = cleo ? reinterpret_cast<CreateCustomScript>(GetProcAddress(cleo, "_CLEO_CreateCustomScript@12")) : nullptr;
	if (!create) {
		Log("Could not run CLEO script " + path + ": CLEO is not installed");
		return;
	}

	// Un archivo que falta se avisa aquí, antes de pedírselo a CLEO.
	std::error_code error;
	const std::filesystem::path file(path);
	const std::filesystem::path full = file.is_absolute() ? file : std::filesystem::path(L"cleo") / file;
	if (!std::filesystem::is_regular_file(full, error)) {
		Log("Could not run CLEO script " + path + ": file not found");
		return;
	}

	// Se llama solo desde Update, fuera del bucle de scripts del juego.
	Log(std::string(create(nullptr, path.c_str(), 0) ? "Started CLEO script " : "CLEO refused to start script ") + path);
}

void MailSystem::Deliver(std::string from, std::string subject, std::string body) {
	store_.Add(std::move(from), std::move(subject), std::move(body), GameDate());
}

void MailSystem::HandleApiRequests() {
	bool changed = false;

	for (api::Request& request : api::Drain()) {
		switch (request.kind) {
		case api::RequestKind::Email: {
			// Un correo con respuesta solo llega una vez por partida.
			if (!request.replyKey.empty() && store_.KnowsReplyKey(request.replyKey)) break;

			const std::string sender = request.from;
			const std::string subject = request.subject;
			if (request.replyKey.empty()) {
				Deliver(std::move(request.from), std::move(request.subject), std::move(request.body));
			}
			else {
				store_.AddWithReplies(std::move(request.from), std::move(request.subject), std::move(request.body), GameDate(),
					request.replyKey, std::move(request.goodText), std::move(request.badText), std::move(request.goodScript), std::move(request.badScript));
				api::SetReply(request.replyKey, 0);
			}
			changed = true;

			if (request.notify == EF_NOTIFY_TEXT) ++newMailToAnnounce_;
			else if (request.notify == EF_NOTIFY_PAGER) {
				pages_.push_back({ sender, "NEW EMAIL: " + subject });
				store_.AddPagerEntry(sender, "NEW EMAIL: " + subject, GameDate());
			}
			break;
		}
		case api::RequestKind::RunScript:
			RunCleoScript(request.body);
			break;
		case api::RequestKind::Text:
			texts_.push_back(ToPlainAscii(request.body));
			break;
		case api::RequestKind::Page:
			store_.AddPagerEntry(request.from, request.body, GameDate());
			pages_.push_back({ std::move(request.from), std::move(request.body) });
			break;
		}
	}

	if (changed) PublishUnreadCount();

	for (const auto& [key, value] : api::DrainValueChanges()) store_.SetValue(key, value);
}

void MailSystem::HandleBrowserOperations() {
	bool changed = false;

	// Antes que los scripts: una página puede guardar un valor y lanzar el script que lo lee.
	std::string valueKey;
	std::string valueText;
	while (overlay_->TakeValueOp(valueKey, valueText)) {
		if (!api::IsValidValueKey(valueKey)) continue;
		store_.SetValue(valueKey, valueText);
		api::SetValueFromGame(valueKey, valueText);
	}

	ef::MailOp operation;
	while (overlay_->TakeMailOp(operation)) {
		if (operation.kind == ef::MailOpKind::MarkRead) changed |= store_.MarkRead(operation.id);
		else if (operation.kind == ef::MailOpKind::Remove) changed |= store_.Remove(operation.id);
		else {
			const int answer = operation.kind == ef::MailOpKind::ReplyGood ? EF_REPLY_GOOD : EF_REPLY_BAD;
			std::string key;
			std::string script;
			if (store_.Answer(operation.id, answer, key, script, GameDate())) {
				changed = true;
				api::SetReply(key, answer);
				if (!script.empty()) RunCleoScript(script);
			}
		}
	}

	// Un botón de un sitio local: solo llegan scripts de la carpeta de ese sitio.
	std::string siteScript;
	while (overlay_->TakeSiteScript(siteScript)) {
		const size_t slash = siteScript.find('/');
		if (slash == std::string::npos) continue;

		const std::filesystem::path relative = std::filesystem::path(siteScript.substr(0, slash)) / L"scripts" / (siteScript.substr(slash + 1) + ".efs");
		const std::filesystem::path systemFile = std::filesystem::path(PluginDirectory()) / L"browser" / L"sites" / relative;
		const std::filesystem::path publicFile = std::filesystem::path(PluginDirectory()) / L"sites" / relative;

		std::error_code error;
		RunCleoScript((std::filesystem::is_regular_file(systemFile, error) ? systemFile : publicFile).string());
	}

	if (changed) PublishUnreadCount();
}

void MailSystem::ShowNextText() {
	// Si ya lo ha leído o borrado todo, no queda nada que anunciar.
	if (newMailToAnnounce_ > 0 && store_.UnreadCount() == 0) newMailToAnnounce_ = 0;
	if (newMailToAnnounce_ == 0 && texts_.empty()) return;

	// Un momento entre avisos, para que el juego llegue a mostrar el anterior.
	if (GetTickCount() - lastTextShown_ < 1000) return;

	// El cuadro de ayuda queda detrás del navegador, así que espera a que se cierre.
	if (overlay_->IsOpen() || !GameCanShowNotifications() || CHud::HelpMessageDisplayed()) return;

	std::string text;
	if (newMailToAnnounce_ > 0) {
		// Se cuentan todos los que hay sin leer: es lo que le importa al jugador.
		const int unread = (std::max)(store_.UnreadCount(), 1);
		text = unread == 1 ? "You have 1 new email." : "You have " + std::to_string(unread) + " new emails.";
	}
	else {
		text = texts_.front();
	}

	CHud::SetHelpMessage(text.c_str(), false, false, false);

	// El juego rechaza el mensaje en algunos momentos: entonces se vuelve a intentar.
	if (!HelpMessageAccepted(text)) return;
	lastTextShown_ = GetTickCount();
	Log("Help box: " + text);
	if (newMailToAnnounce_ > 0) newMailToAnnounce_ = 0;
	else texts_.pop_front();
}

void MailSystem::ShowMemoryEntry() {
	const auto& entries = store_.PagerEntries();
	if (entries.empty()) {
		pager_.ShowFromMemory("", "NO MESSAGES", "");
		return;
	}

	// "Day 568, 13:51" no cabe junto a la posición.
	const PagerEntry& entry = entries[entries.size() - 1 - memoryIndex_];
	std::string when = entry.received;
	if (when.rfind("Day ", 0) == 0) when = "D" + when.substr(4);
	const size_t comma = when.find(", ");
	if (comma != std::string::npos) when.replace(comma, 2, " ");

	pager_.ShowFromMemory(entry.sender, entry.text, when + " " + std::to_string(memoryIndex_ + 1) + "/" + std::to_string(entries.size()));
}

// Lo llama el procedimiento de ventana, en el mismo hilo que el juego. Devuelve true si la tecla era del busca.
bool MailSystem::HandleKey(unsigned int key, bool repeat) {
	if (key == 0 || !GameCanShowNotifications()) return false;

	if (key == keys_.pagerOpen) {
		// Con el ordenador abierto, una letra o un número son para escribir.
		const bool printable = (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z');
		if (overlay_->IsOpen() && printable) return false;
		if (repeat) return true;

		if (memoryOpen_ && pager_.IsBusy()) {
			pager_.Close();
			memoryOpen_ = false;
			return true;
		}

		memoryOpen_ = true;
		memoryIndex_ = 0;
		ShowMemoryEntry();
		return true;
	}

	// Las teclas de avance solo son del busca mientras está en pantalla.
	if (!memoryOpen_ || !pager_.IsBusy()) return false;
	if (key != keys_.pagerOlder && key != keys_.pagerNewer) return false;
	if (repeat) return true;

	const size_t count = store_.PagerEntries().size();
	if (key == keys_.pagerOlder && memoryIndex_ + 1 < count) ++memoryIndex_;
	else if (key == keys_.pagerNewer && memoryIndex_ > 0) --memoryIndex_;

	// Aunque no haya más mensajes, seguir pulsando mantiene el busca abierto.
	ShowMemoryEntry();
	return true;
}

void MailSystem::ShowNextPage() {
	if (memoryOpen_ && !pager_.IsBusy()) memoryOpen_ = false;
	if (pages_.empty() || pager_.IsBusy() || !GameCanShowNotifications()) return;

	pager_.Show(pages_.front().sender, pages_.front().text);
	pages_.pop_front();
	PagerRenderer::PlayBeep();
}

void MailSystem::PublishGameState() {
	ef::GameStateData state{};

	CPed* player = FindPlayerPed(-1);
	if (player) {
		state.inGame = 1;
		state.money = CWorld::Players[0].m_nMoney;
		state.health = player->m_fHealth;
		state.armor = player->m_fArmour;
		if (CWanted* wanted = FindPlayerWanted(-1)) state.wantedLevel = static_cast<int32_t>(wanted->m_nWantedLevel);
		state.day = static_cast<int32_t>(CStats::GetStatValue(STAT_DAYS_PASSED_IN_GAME));
		state.hour = CClock::ms_nGameClockHours;
		state.minute = CClock::ms_nGameClockMinutes;

		CVector position = player->GetPosition();
		state.x = position.x;
		state.y = position.y;
		state.z = position.z;
		state.interior = player->m_nAreaCode;
		state.inVehicle = player->bInVehicle ? 1 : 0;
		state.onMission = CTheScripts::IsPlayerOnAMission() ? 1 : 0;
		state.weather = CWeather::NewWeatherType;

		CZone* zone = nullptr;
		CTheZones::GetZoneInfo(&position, &zone);
		if (zone) {
			if (const char* name = zone->GetTranslatedName()) strncpy_s(state.zone, ToPlainAscii(name).c_str(), _TRUNCATE);
		}
		strncpy_s(state.lastMission, CStats::LastMissionPassedName, 7);
	}

	overlay_->PublishGameState(state);

	if (!stateSampleLogged_ && player && GetTickCount() >= stateSampleTime_) {
		stateSampleLogged_ = true;
		char line[256];
		snprintf(line, sizeof(line), "Game state sample: money %d, health %.0f, armor %.0f, wanted %d, day %d, %02d:%02d, zone '%s', last mission '%s', on mission %d",
			state.money, state.health, state.armor, state.wantedLevel, state.day, state.hour, state.minute, state.zone, state.lastMission, state.onMission);
		Log(line);
	}
}

void MailSystem::Update() {
	if (needsWelcome_) {
		needsWelcome_ = false;
		Deliver("EyeFinder Staff", "Welcome to EyeMail",
			"Welcome to the information superhighway!\n\n"
			"This is your new electronic mailbox. When somebody writes to you, you'll find it here. "
			"Delete what you don't need, because storage is expensive.\n\n"
			"While you're here, why not search the web? https://www.eyefinder.info/\n\n"
			"The EyeFinder Team");
		PublishUnreadCount();
	}

	HandleApiRequests();
	HandleBrowserOperations();
	ShowNextText();
	ShowNextPage();

	const DWORD now = GetTickCount();
	if (now - lastStatePublished_ >= 200) {
		lastStatePublished_ = now;
		PublishGameState();
	}

	// Un script puede cambiar valores en cada fotograma: el disco no tiene por qué enterarse de todos.
	if (now - lastFlush_ >= 250) {
		lastFlush_ = now;
		store_.FlushSession();
	}
}

void MailSystem::Draw() {
	const DWORD now = GetTickCount();
	// El tiempo del busca se detiene con el menú o tras un parón largo: el mensaje no se pierde.
	const DWORD elapsed = (std::min<DWORD>)(now - lastDraw_, 100);
	lastDraw_ = now;

	if (!pager_.IsBusy() || FrontEndMenuManager.m_bMenuActive) return;

	auto* device = reinterpret_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());
	if (!device) return;

	pager_.Advance(elapsed);
	pager_.Draw(device, static_cast<float>(RsGlobal.maximumWidth), static_cast<float>(RsGlobal.maximumHeight));
}
