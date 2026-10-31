#include "MailStore.h"

#include <windows.h>

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

constexpr int kFileVersion = 1;
constexpr size_t kMaxMails = 200;

// Escribe a un temporal y lo sustituye, para que nadie lea nunca un archivo a medias.
bool WriteAtomically(const std::filesystem::path& path, const std::string& text) {
	std::error_code error;
	std::filesystem::create_directories(path.parent_path(), error);

	std::filesystem::path temporary = path;
	temporary += L".tmp";
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		file << text;
		if (!file.good()) return false;
	}
	return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::string Serialize(const std::vector<Mail>& mails, uint32_t nextId) {
	json list = json::array();
	for (const Mail& mail : mails) {
		list.push_back({
			{ "id", mail.id }, { "from", mail.from }, { "subject", mail.subject },
			{ "body", mail.body }, { "received", mail.received }, { "read", mail.read },
		});
	}

	// Un objeto por sistema: otras funciones del mod podrán guardar sus datos aquí.
	const json root = { { "version", kFileVersion }, { "mail", { { "nextId", nextId }, { "mails", list } } } };
	// Otro mod puede mandar UTF-8 no válido: se sustituye en lugar de fallar.
	return root.dump(1, '\t', false, json::error_handler_t::replace);
}

}  // namespace

void MailStore::Clear() {
	mails_.clear();
	nextId_ = 1;
	dirty_ = true;
}

uint32_t MailStore::Add(std::string from, std::string subject, std::string body, std::string received) {
	Mail mail;
	mail.id = nextId_++;
	mail.from = std::move(from);
	mail.subject = std::move(subject);
	mail.body = std::move(body);
	mail.received = std::move(received);
	mails_.push_back(std::move(mail));

	// Si la bandeja se llena, se pierde lo más antiguo, y preferiblemente lo ya leído.
	if (mails_.size() > kMaxMails) {
		auto oldest = std::find_if(mails_.begin(), mails_.end(), [](const Mail& item) { return item.read; });
		mails_.erase(oldest != mails_.end() ? oldest : mails_.begin());
	}

	dirty_ = true;
	return mails_.empty() ? 0 : mails_.back().id;
}

bool MailStore::MarkRead(uint32_t id) {
	for (Mail& mail : mails_) {
		if (mail.id == id && !mail.read) {
			mail.read = true;
			dirty_ = true;
			return true;
		}
	}
	return false;
}

bool MailStore::Remove(uint32_t id) {
	const auto found = std::find_if(mails_.begin(), mails_.end(), [id](const Mail& mail) { return mail.id == id; });
	if (found == mails_.end()) return false;
	mails_.erase(found);
	dirty_ = true;
	return true;
}

int MailStore::UnreadCount() const {
	return static_cast<int>(std::count_if(mails_.begin(), mails_.end(), [](const Mail& mail) { return !mail.read; }));
}

void MailStore::FlushSession() {
	if (!dirty_ || sessionPath_.empty()) return;
	if (WriteAtomically(sessionPath_, Serialize(mails_, nextId_))) dirty_ = false;
}

bool MailStore::SaveTo(const std::filesystem::path& path) const {
	return WriteAtomically(path, Serialize(mails_, nextId_));
}

bool MailStore::LoadFrom(const std::filesystem::path& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) return false;

	try {
		const json root = json::parse(file);
		const json& mail = root.at("mail");

		std::vector<Mail> loaded;
		for (const json& item : mail.at("mails")) {
			Mail entry;
			entry.id = item.at("id").get<uint32_t>();
			entry.from = item.value("from", "");
			entry.subject = item.value("subject", "");
			entry.body = item.value("body", "");
			entry.received = item.value("received", "");
			entry.read = item.value("read", false);
			loaded.push_back(std::move(entry));
		}

		uint32_t nextId = mail.value("nextId", 1u);
		for (const Mail& entry : loaded) nextId = (std::max)(nextId, entry.id + 1);

		mails_ = std::move(loaded);
		nextId_ = nextId;
		dirty_ = true;
		return true;
	}
	catch (const std::exception&) {
		file.close();
		std::filesystem::path damaged = path;
		damaged += L".damaged";
		MoveFileExW(path.c_str(), damaged.c_str(), MOVEFILE_REPLACE_EXISTING);
		return false;
	}
}
