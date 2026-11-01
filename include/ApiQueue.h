#pragma once
// Queue between the public API, which can be called from any thread, and the game thread.
#include <map>
#include <string>
#include <vector>

namespace api {

enum class RequestKind { Email, Text, Page, RunScript };

struct Request {
	RequestKind kind = RequestKind::Text;
	std::string from;     // Sender of the email or pager message.
	std::string subject;
	std::string body;     // Body of the email, or text of the notification.
	int notify = 0;

	// Emails with replies.
	std::string replyKey;
	std::string goodText;
	std::string badText;
	std::string goodScript;
	std::string badScript;
};

// Only from the game thread.
std::vector<Request> Drain();
void SetUnreadCount(int count);

// What the player has answered in this game, for EF_GetReply from any thread.
void ClearReplies();
void SetReply(const std::string& key, int answer);

// Shared values. The API sees them at once, and the game picks them up on the game thread.
bool IsValidValueKey(const std::string& key);
std::map<std::string, std::string> DrainValueChanges();
void ReplaceValues(const std::map<std::string, std::string>& values);
void SetValueFromGame(const std::string& key, const std::string& value);

}  // namespace api
