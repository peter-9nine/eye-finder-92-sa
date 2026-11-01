#pragma once
// The inbox and its file. It doesn't depend on the game, so the harness uses it too.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct Mail {
	uint32_t id = 0;
	std::string from;
	std::string subject;
	std::string body;
	std::string received;
	bool read = false;

	// The two answers offered by the email's sender. Without them, it can't be answered.
	std::string replyKey;
	std::string goodText;
	std::string badText;
	std::string goodScript;
	std::string badScript;
	int answer = 0;  // 0 unanswered, 1 good, 2 bad
	std::string answeredAt;
};

struct PagerEntry {
	std::string sender;
	std::string text;
	std::string received;
};

class MailStore {
public:
	// The session file always reflects the current game, and the helper reads it from there.
	void SetSessionPath(std::filesystem::path path) { sessionPath_ = std::move(path); }

	void Clear();
	uint32_t Add(std::string from, std::string subject, std::string body, std::string received);
	uint32_t AddWithReplies(std::string from, std::string subject, std::string body, std::string received,
		std::string replyKey, std::string goodText, std::string badText, std::string goodScript, std::string badScript);

	// Returns true only the first time, because an answer can't change. Any script is left in script.
	bool Answer(uint32_t id, int answer, std::string& key, std::string& script, std::string answeredAt = {});
	bool KnowsReplyKey(const std::string& key) const { return replies_.count(key) != 0; }
	const std::map<std::string, int>& Replies() const { return replies_; }
	bool MarkRead(uint32_t id);

	// The pager's memory, from oldest to newest.
	void AddPagerEntry(std::string sender, std::string text, std::string received);
	const std::vector<PagerEntry>& PagerEntries() const { return pagerEntries_; }

	// Values shared with mods and pages: they're saved with the game, like the mail.
	void SetValue(const std::string& key, const std::string& value);
	const std::map<std::string, std::string>& Values() const { return values_; }
	bool Remove(uint32_t id);

	int UnreadCount() const;
	const std::vector<Mail>& Mails() const { return mails_; }

	// Writes the session file if anything changed. If Windows has it busy, it tries again on the next call.
	void FlushSession();

	// Saved game: a complete copy for each of the game's save files.
	bool SaveTo(const std::filesystem::path& path) const;
	// A damaged file is set aside and not loaded: it must never stop anyone playing.
	bool LoadFrom(const std::filesystem::path& path);

private:
	std::filesystem::path sessionPath_;
	std::vector<Mail> mails_;
	// Survives deleting the email, so the mod that sent it can still ask.
	std::map<std::string, int> replies_;
	std::map<std::string, std::string> values_;
	std::vector<PagerEntry> pagerEntries_;
	uint32_t nextId_ = 1;
	bool dirty_ = true;
};
