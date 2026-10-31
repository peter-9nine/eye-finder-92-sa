// EyeFinder92SA public API, version 1. Copy this file into your mod.
//
// Every function is safe to call from any thread and at any time. Calls made before a
// game is running wait in a queue. Text is UTF-8. If EyeFinder92SA isn't installed,
// the helpers below do nothing and return 0.
//
// EyeFinder92SA may load after your mod, so don't call these from DllMain or a global
// constructor. Call them once the game is running.
//
// Not using C++? Call GetModuleHandleA("EyeFinder92SA.asi") and GetProcAddress with the
// function names below. They're all __cdecl.
#pragma once
#include <windows.h>

#define EF_API_VERSION 1

// How the player finds out about a new email.
#define EF_NOTIFY_NONE 0   // Only the unread count on the computer changes.
#define EF_NOTIFY_TEXT 1   // "You have 3 new emails" in the game's help box.
#define EF_NOTIFY_PAGER 2  // The pager slides in and beeps.

#ifdef __cplusplus
extern "C" {
#endif

// int EF_GetApiVersion(void)
// Returns EF_API_VERSION of the installed mod.
typedef int(__cdecl* EF_GetApiVersion_t)(void);

// int EF_SendEmail(const char* from, const char* subject, const char* body, int notify)
// Delivers an email to the player's inbox. It's saved with the player's game. The body is
// plain text, and web addresses in it become links. Returns 1 if accepted.
typedef int(__cdecl* EF_SendEmail_t)(const char* from, const char* subject, const char* body, int notify);

// int EF_Notify(const char* text)
// Shows a text in the game's help box, without an email. Returns 1 if accepted.
typedef int(__cdecl* EF_Notify_t)(const char* text);

// int EF_Page(const char* sender, const char* text)
// Sends a message to the pager, without an email. Returns 1 if accepted.
typedef int(__cdecl* EF_Page_t)(const char* sender, const char* text);

// int EF_GetUnreadCount(void)
// Returns how many emails in the player's inbox are unread.
typedef int(__cdecl* EF_GetUnreadCount_t)(void);

#ifdef __cplusplus
}

namespace eyefinder {

template <typename Function>
inline Function Find(const char* name) {
	const HMODULE module = GetModuleHandleA("EyeFinder92SA.asi");
	return module ? reinterpret_cast<Function>(GetProcAddress(module, name)) : nullptr;
}

inline bool IsInstalled() { return GetModuleHandleA("EyeFinder92SA.asi") != nullptr; }

inline int GetApiVersion() {
	const auto function = Find<EF_GetApiVersion_t>("EF_GetApiVersion");
	return function ? function() : 0;
}

inline int SendEmail(const char* from, const char* subject, const char* body, int notify = EF_NOTIFY_TEXT) {
	const auto function = Find<EF_SendEmail_t>("EF_SendEmail");
	return function ? function(from, subject, body, notify) : 0;
}

inline int Notify(const char* text) {
	const auto function = Find<EF_Notify_t>("EF_Notify");
	return function ? function(text) : 0;
}

inline int Page(const char* sender, const char* text) {
	const auto function = Find<EF_Page_t>("EF_Page");
	return function ? function(sender, text) : 0;
}

inline int GetUnreadCount() {
	const auto function = Find<EF_GetUnreadCount_t>("EF_GetUnreadCount");
	return function ? function() : 0;
}

}  // namespace eyefinder
#endif
