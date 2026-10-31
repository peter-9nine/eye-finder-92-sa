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
	store_.FlushSession();
	PublishUnreadCount();
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
	PublishUnreadCount();
}

void MailSystem::Deliver(std::string from, std::string subject, std::string body) {
	store_.Add(std::move(from), std::move(subject), std::move(body), GameDate());
}

void MailSystem::HandleApiRequests() {
	bool changed = false;

	for (api::Request& request : api::Drain()) {
		switch (request.kind) {
		case api::RequestKind::Email: {
			const std::string sender = request.from;
			const std::string subject = request.subject;
			Deliver(std::move(request.from), std::move(request.subject), std::move(request.body));
			changed = true;

			if (request.notify == EF_NOTIFY_TEXT) ++newMailToAnnounce_;
			else if (request.notify == EF_NOTIFY_PAGER) pages_.push_back({ sender, "NEW EMAIL: " + subject });
			break;
		}
		case api::RequestKind::Text:
			texts_.push_back(ToPlainAscii(request.body));
			break;
		case api::RequestKind::Page:
			pages_.push_back({ std::move(request.from), std::move(request.body) });
			break;
		}
	}

	if (changed) PublishUnreadCount();
}

void MailSystem::HandleBrowserOperations() {
	bool changed = false;

	ef::MailOp operation;
	while (overlay_->TakeMailOp(operation)) {
		if (operation.kind == ef::MailOpKind::MarkRead) changed |= store_.MarkRead(operation.id);
		else if (operation.kind == ef::MailOpKind::Remove) changed |= store_.Remove(operation.id);
	}

	if (changed) PublishUnreadCount();
}

void MailSystem::ShowNextText() {
	if (newMailToAnnounce_ == 0 && texts_.empty()) return;

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
	if (!CHud::HelpMessageDisplayed()) return;
	if (newMailToAnnounce_ > 0) newMailToAnnounce_ = 0;
	else texts_.pop_front();
}

void MailSystem::ShowNextPage() {
	if (pages_.empty() || pager_.IsBusy() || !GameCanShowNotifications()) return;

	pager_.Show(pages_.front().sender, pages_.front().text);
	pages_.pop_front();
	PagerRenderer::PlayBeep();
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
	store_.FlushSession();
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
