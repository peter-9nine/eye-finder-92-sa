#include "pch.h"
#include "MailSystem.h"
#include "ApiQueue.h"
#include "EyeFinderApi.h"
#include "Paths.h"

namespace {

// The game sees the button too, so opening the pager can't be a simple press.
constexpr DWORD kPadPagerHoldMs = 500;

// Notifications must not interrupt cutscenes or appear over the menu.
bool GameCanShowNotifications() {
	return !FrontEndMenuManager.m_bMenuActive && !TheCamera.m_bWideScreenOn && !CCutsceneMgr::ms_running;
}

// The game only copies the text here if it accepts it. HelpMessageDisplayed won't do: it stays false
// until the box is drawn, which is why the message used to repeat forever.
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

void MailSystem::Initialize(BrowserOverlay& overlay, const PadInput& pad) {
	overlay_ = &overlay;
	pad_ = &pad;
	saveFolder_ = SaveDataDirectory();
	store_.SetSessionPath(SessionSavePath());

	std::string problems;
	keys_ = LoadKeyConfig(PluginDirectory() + L"\\EyeFinder92SA.ini", problems);
	if (!problems.empty()) Log(problems);
	problems.clear();
	overlay.SetLocations(LoadLocations(PluginDirectory() + L"\\locations.json", problems));
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
		// A save from before the mod was installed, or a damaged file.
		Log("No mod data for " + path.filename().string() + ": starting with an empty inbox");
	}

	store_.FlushSession();
	SyncReplies();
	api::ReplaceValues(store_.Values());
	PublishUnreadCount();
}

void MailSystem::RunCleoScript(const std::string& path) {
	// CLEO may not be installed, so it's looked up on each use and never linked.
	using CreateCustomScript = void*(__stdcall*)(void* fromThread, const char* filePath, int label);
	const HMODULE cleo = GetModuleHandleA("CLEO.asi");
	const auto create = cleo ? reinterpret_cast<CreateCustomScript>(GetProcAddress(cleo, "_CLEO_CreateCustomScript@12")) : nullptr;
	if (!create) {
		Log("Could not run CLEO script " + path + ": CLEO is not installed");
		return;
	}

	// A missing file is reported here, before asking CLEO for it.
	std::error_code error;
	const std::filesystem::path file(path);
	const std::filesystem::path full = file.is_absolute() ? file : std::filesystem::path(L"cleo") / file;
	if (!std::filesystem::is_regular_file(full, error)) {
		Log("Could not run CLEO script " + path + ": file not found");
		return;
	}

	// Only called from Update, outside the game's script loop.
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
			// An email with replies only arrives once per saved game.
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

	// Before the scripts: a page can store a value and start the script that reads it.
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

	// A button on a local site: only scripts from that site's folder arrive here.
	std::string siteScript;
	while (overlay_->TakeSiteScript(siteScript)) {
		const size_t slash = siteScript.find('/');
		if (slash == std::string::npos) continue;

		const std::filesystem::path relative = std::filesystem::path(siteScript.substr(0, slash)) / L"scripts" / (siteScript.substr(slash + 1) + ".efs");
		const std::filesystem::path mod(PluginDirectory());
		std::filesystem::path file = mod / L"sites" / relative;
		for (const std::filesystem::path& candidate : { mod / L"system" / L"sites" / relative, mod / L"system" / L"sites" / L"hidden" / relative, mod / L"sites" / L"hidden" / relative }) {
			std::error_code error;
			if (std::filesystem::is_regular_file(candidate, error)) {
				file = candidate;
				break;
			}
		}
		RunCleoScript(file.string());
	}

	// A page calling a mod's C++ function. After the values, so a page can store one and then call.
	static ef::SiteCall call;  // Large, and only ever used on the game thread.
	while (overlay_->TakeSiteCall(call)) {
		std::string result;
		const api::FunctionResult outcome = api::RunSiteFunction(call.domain, call.name, call.args, result);
		if (outcome == api::FunctionResult::Failed) Log(std::string("Site function failed with an exception: ") + call.domain + "/" + call.name);
		overlay_->SendSiteCallResult(call.id, static_cast<ef::SiteCallStatus>(outcome), result);
	}

	if (changed) PublishUnreadCount();
}

void MailSystem::ShowNextText() {
	// If everything has been read or deleted, there's nothing left to announce.
	if (newMailToAnnounce_ > 0 && store_.UnreadCount() == 0) newMailToAnnounce_ = 0;
	if (newMailToAnnounce_ == 0 && texts_.empty()) return;

	// A moment between messages, so that the game gets to show the previous one.
	if (GetTickCount() - lastTextShown_ < 1000) return;

	// The help box stays behind the browser, so it waits for it to close.
	if (overlay_->IsOpen() || !GameCanShowNotifications() || CHud::HelpMessageDisplayed()) return;

	std::string text;
	if (newMailToAnnounce_ > 0) {
		// Counts all the unread ones, which is what matters to the player.
		const int unread = (std::max)(store_.UnreadCount(), 1);
		text = unread == 1 ? "You have 1 new email." : "You have " + std::to_string(unread) + " new emails.";
	}
	else {
		text = texts_.front();
	}

	CHud::SetHelpMessage(text.c_str(), false, false, false);

	// The game refuses the message at times, so it's tried again.
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

	// "Day 568, 13:51" doesn't fit next to the position.
	const PagerEntry& entry = entries[entries.size() - 1 - memoryIndex_];
	std::string when = entry.received;
	if (when.rfind("Day ", 0) == 0) when = "D" + when.substr(4);
	const size_t comma = when.find(", ");
	if (comma != std::string::npos) when.replace(comma, 2, " ");

	pager_.ShowFromMemory(entry.sender, entry.text, when + " " + std::to_string(memoryIndex_ + 1) + "/" + std::to_string(entries.size()));
}

// Called by the window procedure, on the game's thread. Returns true if the key belonged to the pager.
bool MailSystem::HandleKey(unsigned int key, bool repeat) {
	if (key == 0 || !GameCanShowNotifications()) return false;

	if (key == keys_.pagerOpen) {
		// With the computer open, a letter or number is for typing.
		const bool printable = (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z');
		if (overlay_->IsOpen() && printable) return false;
		if (!repeat) TogglePagerMemory();
		return true;
	}

	// The browsing keys only belong to the pager while it's on screen.
	if (!memoryOpen_ || !pager_.IsBusy()) return false;
	if (key != keys_.pagerOlder && key != keys_.pagerNewer) return false;
	if (!repeat) StepPagerMemory(key == keys_.pagerOlder);
	return true;
}

void MailSystem::TogglePagerMemory() {
	if (memoryOpen_ && pager_.IsBusy()) {
		pager_.Close();
		memoryOpen_ = false;
		return;
	}

	memoryOpen_ = true;
	memoryIndex_ = 0;
	ShowMemoryEntry();
}

void MailSystem::StepPagerMemory(bool older) {
	const size_t count = store_.PagerEntries().size();
	if (older && memoryIndex_ + 1 < count) ++memoryIndex_;
	else if (!older && memoryIndex_ > 0) --memoryIndex_;

	// Even with no more messages, pressing again keeps the pager up.
	ShowMemoryEntry();
}

// Holding D-pad left opens or closes the pager. With it on screen, left and right browse it.
void MailSystem::UpdatePad() {
	// The on-screen keyboard's D-pad must not move the pager.
	if (!GameCanShowNotifications() || overlay_->KeyboardOpen()) {
		padHoldStart_ = 0;
		return;
	}

	const bool browsing = memoryOpen_ && pager_.IsBusy();
	if (browsing && pad_->Pressed(kPadDPadRight)) StepPagerMemory(false);

	if (pad_->Pressed(kPadDPadLeft)) {
		padHoldStart_ = GetTickCount();
		if (browsing) StepPagerMemory(true);
	}
	else if (!pad_->Held(kPadDPadLeft)) {
		padHoldStart_ = 0;
	}

	if (padHoldStart_ != 0 && GetTickCount() - padHoldStart_ >= kPadPagerHoldMs) {
		// Only once per press: it has to be released before using it again.
		padHoldStart_ = 0;
		TogglePagerMemory();
	}
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

	std::string receipt;
	if (overlay_->TakeSessionReceipt(receipt)) texts_.push_back(std::move(receipt));

	ShowNextText();
	UpdatePad();
	ShowNextPage();

	const DWORD now = GetTickCount();
	if (now - lastStatePublished_ >= 200) {
		lastStatePublished_ = now;
		PublishGameState();
	}

	// A script can change values every frame, and the disk doesn't need to hear about all of them.
	if (now - lastFlush_ >= 250) {
		lastFlush_ = now;
		store_.FlushSession();
	}
}

void MailSystem::Draw() {
	const DWORD now = GetTickCount();
	// The pager's time stops with the menu or after a long stall, so the message isn't lost.
	const DWORD elapsed = (std::min<DWORD>)(now - lastDraw_, 100);
	lastDraw_ = now;

	if (!pager_.IsBusy() || FrontEndMenuManager.m_bMenuActive) return;

	auto* device = reinterpret_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());
	if (!device) return;

	pager_.Advance(elapsed);
	pager_.Draw(device, static_cast<float>(RsGlobal.maximumWidth), static_cast<float>(RsGlobal.maximumHeight));
}
