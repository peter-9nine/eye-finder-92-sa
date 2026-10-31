// EyeFinder92SA public API, version 3. Copy this file into your mod.
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

#define EF_API_VERSION 3

// How the player finds out about a new email.
#define EF_NOTIFY_NONE 0   // Only the unread count on the computer changes.
#define EF_NOTIFY_TEXT 1   // "You have 3 new emails" in the game's help box.
#define EF_NOTIFY_PAGER 2  // The pager slides in and beeps.

// What the player answered to an email.
#define EF_REPLY_NONE 0  // Not answered yet, or the key is unknown.
#define EF_REPLY_GOOD 1
#define EF_REPLY_BAD 2

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
// Sends a message to the pager, without an email. The pager remembers its last 10 messages,
// which the player can bring back up. Returns 1 if accepted.
typedef int(__cdecl* EF_Page_t)(const char* sender, const char* text);

// int EF_GetUnreadCount(void)
// Returns how many emails in the player's inbox are unread.
typedef int(__cdecl* EF_GetUnreadCount_t)(void);

// Added in API version 2.
//
// int EF_SendEmailWithReplies(from, subject, body, notify, replyKey, goodText, badText, goodScript, badScript)
// Sends an email that the player answers with one of two buttons, showing goodText and badText.
// - replyKey identifies this email, for EF_GetReply. Make it unique, for example "mymod_smoke_dinner".
//   It's saved with the player's game, and sending the same key again in that game does nothing, so
//   it's safe to send a story email every time your mod starts. Returns 1 in that case too.
// - goodScript and badScript are optional CLEO scripts to start when the player picks that answer.
//   Pass NULL for none. A relative path starts in the game's cleo folder.
typedef int(__cdecl* EF_SendEmailWithReplies_t)(const char* from, const char* subject, const char* body, int notify,
	const char* replyKey, const char* goodText, const char* badText, const char* goodScript, const char* badScript);

// int EF_GetReply(const char* replyKey)
// Returns EF_REPLY_NONE, EF_REPLY_GOOD or EF_REPLY_BAD, for the game being played.
typedef int(__cdecl* EF_GetReply_t)(const char* replyKey);

// int EF_RunCleoScript(const char* path)
// Starts a CLEO script on the game's next frame. A relative path starts in the game's cleo folder.
// Returns 1 if accepted. It's refused if CLEO isn't installed, or later if the file can't be found.
typedef int(__cdecl* EF_RunCleoScript_t)(const char* path);

// Added in API version 3.
//
// Shared values are named pieces of data that mods, CLEO scripts and web pages can all read and
// write. They're saved with the player's game. Use a prefix to avoid clashes, like "mymod.money".
// Keys use letters, numbers, '.', '_' and '-', up to 63 characters. Text is up to 1023 bytes.
//
// int EF_SetValue(const char* key, const char* text): returns 1 if accepted.
typedef int(__cdecl* EF_SetValue_t)(const char* key, const char* text);

// int EF_SetValueInt(const char* key, int value): returns 1 if accepted.
typedef int(__cdecl* EF_SetValueInt_t)(const char* key, int value);

// int EF_GetValue(const char* key, char* buffer, int bufferSize)
// Copies the text into buffer. Returns 1 if the value exists, or 0 if it doesn't.
typedef int(__cdecl* EF_GetValue_t)(const char* key, char* buffer, int bufferSize);

// int EF_GetValueInt(const char* key, int defaultValue)
// Returns the value as a number, or defaultValue if it doesn't exist or isn't a number.
typedef int(__cdecl* EF_GetValueInt_t)(const char* key, int defaultValue);

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

inline int SendEmailWithReplies(const char* from, const char* subject, const char* body, int notify,
	const char* replyKey, const char* goodText, const char* badText, const char* goodScript = nullptr, const char* badScript = nullptr) {
	const auto function = Find<EF_SendEmailWithReplies_t>("EF_SendEmailWithReplies");
	return function ? function(from, subject, body, notify, replyKey, goodText, badText, goodScript, badScript) : 0;
}

inline int GetReply(const char* replyKey) {
	const auto function = Find<EF_GetReply_t>("EF_GetReply");
	return function ? function(replyKey) : EF_REPLY_NONE;
}

inline int RunCleoScript(const char* path) {
	const auto function = Find<EF_RunCleoScript_t>("EF_RunCleoScript");
	return function ? function(path) : 0;
}

inline int SetValue(const char* key, const char* text) {
	const auto function = Find<EF_SetValue_t>("EF_SetValue");
	return function ? function(key, text) : 0;
}

inline int SetValueInt(const char* key, int value) {
	const auto function = Find<EF_SetValueInt_t>("EF_SetValueInt");
	return function ? function(key, value) : 0;
}

inline int GetValue(const char* key, char* buffer, int bufferSize) {
	const auto function = Find<EF_GetValue_t>("EF_GetValue");
	return function ? function(key, buffer, bufferSize) : 0;
}

inline int GetValueInt(const char* key, int defaultValue = 0) {
	const auto function = Find<EF_GetValueInt_t>("EF_GetValueInt");
	return function ? function(key, defaultValue) : defaultValue;
}

inline int GetUnreadCount() {
	const auto function = Find<EF_GetUnreadCount_t>("EF_GetUnreadCount");
	return function ? function() : 0;
}

}  // namespace eyefinder
#endif
