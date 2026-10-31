// La API pública para otros mods: ver api/EyeFinderApi.h.
#include "ApiQueue.h"

#include <atomic>
#include <cstring>
#include <mutex>

#define EF_EXPORT extern "C" __declspec(dllexport)

namespace {

constexpr size_t kMaxPending = 256;
constexpr size_t kMaxNameLength = 128;
constexpr size_t kMaxSubjectLength = 256;
constexpr size_t kMaxBodyLength = 16 * 1024;

std::mutex gMutex;
std::vector<api::Request> gPending;
std::atomic<int> gUnread{ 0 };

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

EF_EXPORT int __cdecl EF_GetApiVersion() {
	return 1;
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
