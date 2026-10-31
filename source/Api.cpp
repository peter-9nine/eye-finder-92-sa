// La API pública para otros mods: ver api/EyeFinderApi.h.
#include "ApiQueue.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

#define EF_EXPORT extern "C" __declspec(dllexport)

namespace {

constexpr size_t kMaxPending = 256;
constexpr size_t kMaxNameLength = 128;
constexpr size_t kMaxSubjectLength = 256;
constexpr size_t kMaxBodyLength = 16 * 1024;
constexpr size_t kMaxPathLength = 260;
constexpr size_t kMaxKeyLength = 63;
constexpr size_t kMaxValueLength = 1023;
constexpr size_t kMaxValues = 1024;

std::mutex gMutex;
std::vector<api::Request> gPending;
std::atomic<int> gUnread{ 0 };
std::map<std::string, int> gReplies;
std::map<std::string, std::string> gValues;
std::map<std::string, std::string> gValueChanges;

std::string Copy(const char* text, size_t limit) {
	if (!text) return {};
	return std::string(text, strnlen(text, limit));
}

int Enqueue(api::Request&& request) {
	try {
		std::lock_guard lock(gMutex);
		if (gPending.size() >= kMaxPending) return 0;
		gPending.push_back(std::move(request));
		return 1;
	}
	catch (...) {
		// Un fallo aquí nunca debe llegar al mod que nos llama.
		return 0;
	}
}

}  // namespace

std::vector<api::Request> api::Drain() {
	std::lock_guard lock(gMutex);
	std::vector<Request> requests;
	requests.swap(gPending);
	return requests;
}

void api::SetUnreadCount(int count) {
	gUnread.store(count, std::memory_order_relaxed);
}

void api::ClearReplies() {
	std::lock_guard lock(gMutex);
	gReplies.clear();
}

void api::SetReply(const std::string& key, int answer) {
	std::lock_guard lock(gMutex);
	gReplies[key] = answer;
}

bool api::IsValidValueKey(const std::string& key) {
	return !key.empty() && key.size() <= kMaxKeyLength &&
		key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == std::string::npos;
}

std::map<std::string, std::string> api::DrainValueChanges() {
	std::lock_guard lock(gMutex);
	std::map<std::string, std::string> changes;
	changes.swap(gValueChanges);
	return changes;
}

void api::ReplaceValues(const std::map<std::string, std::string>& values) {
	std::lock_guard lock(gMutex);
	gValues = values;
	gValueChanges.clear();
}

void api::SetValueFromGame(const std::string& key, const std::string& value) {
	std::lock_guard lock(gMutex);
	gValues[key] = value;
}

EF_EXPORT int __cdecl EF_GetApiVersion() {
	return 3;
}

EF_EXPORT int __cdecl EF_SendEmail(const char* from, const char* subject, const char* body, int notify) {
	try {
		api::Request request;
		request.kind = api::RequestKind::Email;
		request.from = Copy(from, kMaxNameLength);
		request.subject = Copy(subject, kMaxSubjectLength);
		request.body = Copy(body, kMaxBodyLength);
		request.notify = notify;
		if (request.from.empty() && request.subject.empty() && request.body.empty()) return 0;
		return Enqueue(std::move(request));
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_Notify(const char* text) {
	try {
		api::Request request;
		request.kind = api::RequestKind::Text;
		request.body = Copy(text, kMaxSubjectLength);
		if (request.body.empty()) return 0;
		return Enqueue(std::move(request));
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_Page(const char* sender, const char* text) {
	try {
		api::Request request;
		request.kind = api::RequestKind::Page;
		request.from = Copy(sender, kMaxNameLength);
		request.body = Copy(text, kMaxSubjectLength);
		if (request.body.empty()) return 0;
		return Enqueue(std::move(request));
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_SendEmailWithReplies(const char* from, const char* subject, const char* body, int notify,
	const char* replyKey, const char* goodText, const char* badText, const char* goodScript, const char* badScript) {
	try {
		api::Request request;
		request.kind = api::RequestKind::Email;
		request.from = Copy(from, kMaxNameLength);
		request.subject = Copy(subject, kMaxSubjectLength);
		request.body = Copy(body, kMaxBodyLength);
		request.notify = notify;
		request.replyKey = Copy(replyKey, kMaxNameLength);
		request.goodText = Copy(goodText, kMaxSubjectLength);
		request.badText = Copy(badText, kMaxSubjectLength);
		request.goodScript = Copy(goodScript, kMaxPathLength);
		request.badScript = Copy(badScript, kMaxPathLength);

		// Sin clave ni los dos textos no se puede responder.
		if (request.replyKey.empty() || request.goodText.empty() || request.badText.empty()) return 0;
		return Enqueue(std::move(request));
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_GetReply(const char* replyKey) {
	if (!replyKey) return 0;
	try {
		std::lock_guard lock(gMutex);
		const auto found = gReplies.find(replyKey);
		return found == gReplies.end() ? 0 : found->second;
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_RunCleoScript(const char* path) {
	try {
		api::Request request;
		request.kind = api::RequestKind::RunScript;
		request.body = Copy(path, kMaxPathLength);
		if (request.body.empty()) return 0;
		return Enqueue(std::move(request));
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_SetValue(const char* key, const char* text) {
	try {
		const std::string name = Copy(key, kMaxKeyLength + 1);
		if (!api::IsValidValueKey(name) || !text) return 0;
		const std::string value = Copy(text, kMaxValueLength);

		std::lock_guard lock(gMutex);
		if (gValues.size() >= kMaxValues && gValues.count(name) == 0) return 0;
		gValues[name] = value;
		gValueChanges[name] = value;
		return 1;
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_SetValueInt(const char* key, int value) {
	try {
		return EF_SetValue(key, std::to_string(value).c_str());
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_GetValue(const char* key, char* buffer, int bufferSize) {
	if (!key || !buffer || bufferSize <= 0) return 0;
	buffer[0] = '\0';
	try {
		std::lock_guard lock(gMutex);
		const auto found = gValues.find(key);
		if (found == gValues.end()) return 0;
		strncpy_s(buffer, static_cast<size_t>(bufferSize), found->second.c_str(), _TRUNCATE);
		return 1;
	}
	catch (...) {
		return 0;
	}
}

EF_EXPORT int __cdecl EF_GetValueInt(const char* key, int defaultValue) {
	if (!key) return defaultValue;
	try {
		std::lock_guard lock(gMutex);
		const auto found = gValues.find(key);
		if (found == gValues.end() || found->second.empty()) return defaultValue;

		char* end = nullptr;
		const long number = std::strtol(found->second.c_str(), &end, 10);
		return *end == '\0' ? static_cast<int>(number) : defaultValue;
	}
	catch (...) {
		return defaultValue;
	}
}

EF_EXPORT int __cdecl EF_GetUnreadCount() {
	return gUnread.load(std::memory_order_relaxed);
}

#ifdef EF_API_TEST
// Solo en el doble de pruebas del arnés.
EF_EXPORT int __cdecl EF_Test_PendingCount() {
	std::lock_guard lock(gMutex);
	return static_cast<int>(gPending.size());
}

EF_EXPORT int __cdecl EF_Test_LastEquals(const char* from, const char* subject, const char* body, int notify) {
	std::lock_guard lock(gMutex);
	if (gPending.empty()) return 0;
	const api::Request& last = gPending.back();
	return last.from == from && last.subject == subject && last.body == body && last.notify == notify;
}
#endif
