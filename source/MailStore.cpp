#include "MailStore.h"

#include <windows.h>

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

constexpr int kFileVersion = 1;
constexpr size_t kMaxMails = 200;
constexpr size_t kMaxPagerEntries = 10;

// Writes to a temporary file and swaps it in, so that nobody ever reads a half-written file.
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

std::string Serialize(const std::vector<Mail>& mails, uint32_t nextId, const std::map<std::string, int>& replies, const std::map<std::string, std::string>& values, const std::vector<PagerEntry>& pagerEntries) {
	json pager = json::array();
	for (const PagerEntry& entry : pagerEntries) {
		pager.push_back({ { "sender", entry.sender }, { "text", entry.text }, { "received", entry.received } });
	}

	json list = json::array();
	for (const Mail& mail : mails) {
		json item = {
			{ "id", mail.id }, { "from", mail.from }, { "subject", mail.subject },
			{ "body", mail.body }, { "received", mail.received }, { "read", mail.read },
		};
		if (!mail.replyKey.empty()) {
			item["replyKey"] = mail.replyKey;
			item["goodText"] = mail.goodText;
			item["badText"] = mail.badText;
			item["goodScript"] = mail.goodScript;
			item["badScript"] = mail.badScript;
			item["answer"] = mail.answer;
			item["answeredAt"] = mail.answeredAt;
		}
		list.push_back(std::move(item));
	}

	// One object per system: other mod features can store their data here.
	const json root = { { "version", kFileVersion }, { "mail", { { "nextId", nextId }, { "mails", list }, { "replies", replies } } }, { "values", values }, { "pager", pager } };
	// Another mod can send invalid UTF-8: it's replaced instead of failing.
	return root.dump(1, '\t', false, json::error_handler_t::replace);
}

}  // namespace

void MailStore::Clear() {
	mails_.clear();
	replies_.clear();
	values_.clear();
	pagerEntries_.clear();
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

	// If the inbox fills up, the oldest is lost, preferably something already read.
	if (mails_.size() > kMaxMails) {
		auto oldest = std::find_if(mails_.begin(), mails_.end(), [](const Mail& item) { return item.read; });
		mails_.erase(oldest != mails_.end() ? oldest : mails_.begin());
	}

	dirty_ = true;
	return mails_.empty() ? 0 : mails_.back().id;
}

uint32_t MailStore::AddWithReplies(std::string from, std::string subject, std::string body, std::string received,
	std::string replyKey, std::string goodText, std::string badText, std::string goodScript, std::string badScript) {
	const uint32_t id = Add(std::move(from), std::move(subject), std::move(body), std::move(received));
	Mail& mail = mails_.back();
	replies_[replyKey] = 0;
	mail.replyKey = std::move(replyKey);
	mail.goodText = std::move(goodText);
	mail.badText = std::move(badText);
	mail.goodScript = std::move(goodScript);
	mail.badScript = std::move(badScript);
	dirty_ = true;
	return id;
}

bool MailStore::Answer(uint32_t id, int answer, std::string& key, std::string& script, std::string answeredAt) {
	if (answer != 1 && answer != 2) return false;

	for (Mail& mail : mails_) {
		if (mail.id == id && !mail.replyKey.empty() && mail.answer == 0) {
			mail.answer = answer;
			mail.answeredAt = std::move(answeredAt);
			mail.read = true;
			replies_[mail.replyKey] = answer;
			key = mail.replyKey;
			script = answer == 1 ? mail.goodScript : mail.badScript;
			dirty_ = true;
			return true;
		}
	}
	return false;
}

void MailStore::AddPagerEntry(std::string sender, std::string text, std::string received) {
	pagerEntries_.push_back({ std::move(sender), std::move(text), std::move(received) });
	if (pagerEntries_.size() > kMaxPagerEntries) pagerEntries_.erase(pagerEntries_.begin());
	dirty_ = true;
}

void MailStore::SetValue(const std::string& key, const std::string& value) {
	auto found = values_.find(key);
	if (found != values_.end() && found->second == value) return;
	values_[key] = value;
	dirty_ = true;
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
	if (WriteAtomically(sessionPath_, Serialize(mails_, nextId_, replies_, values_, pagerEntries_))) dirty_ = false;
}

bool MailStore::SaveTo(const std::filesystem::path& path) const {
	return WriteAtomically(path, Serialize(mails_, nextId_, replies_, values_, pagerEntries_));
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
			entry.replyKey = item.value("replyKey", "");
			entry.goodText = item.value("goodText", "");
			entry.badText = item.value("badText", "");
			entry.goodScript = item.value("goodScript", "");
			entry.badScript = item.value("badScript", "");
			entry.answer = item.value("answer", 0);
			entry.answeredAt = item.value("answeredAt", "");
			loaded.push_back(std::move(entry));
		}

		uint32_t nextId = mail.value("nextId", 1u);
		for (const Mail& entry : loaded) nextId = (std::max)(nextId, entry.id + 1);

		std::map<std::string, int> replies = mail.value("replies", std::map<std::string, int>{});
		mails_ = std::move(loaded);
		replies_ = std::move(replies);
		values_ = root.value("values", std::map<std::string, std::string>{});

		pagerEntries_.clear();
		if (root.contains("pager")) {
			for (const json& item : root.at("pager")) {
				pagerEntries_.push_back({ item.value("sender", ""), item.value("text", ""), item.value("received", "") });
			}
		}
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
