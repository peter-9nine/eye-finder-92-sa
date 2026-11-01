#pragma once
// Shared memory protocol between the plugin (32-bit) and the helper (64-bit).
// Fixed-size types only: the layout must be identical on both architectures.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ef {

constexpr uint32_t kMagic = 0x32394645;  // "EF92"
constexpr uint32_t kProtocol = 6;
constexpr uint32_t kSlots = 3;
constexpr uint32_t kNewFrame = 0x80000000u;
constexpr uint32_t kRingSize = 256;
constexpr uint32_t kMailOpRingSize = 64;
constexpr uint32_t kSiteScriptRingSize = 8;
constexpr uint32_t kSiteScriptNameSize = 200;
constexpr uint32_t kValueOpRingSize = 16;
constexpr uint32_t kValueKeySize = 64;
constexpr uint32_t kValueTextSize = 1024;
constexpr uint32_t kHeaderSize = 64 * 1024;

// Same values as CEF's EVENTFLAG_*.
constexpr uint32_t kModShift = 1u << 1;
constexpr uint32_t kModControl = 1u << 2;
constexpr uint32_t kModAlt = 1u << 3;
constexpr uint32_t kModLeftMouse = 1u << 4;
constexpr uint32_t kModMiddleMouse = 1u << 5;
constexpr uint32_t kModRightMouse = 1u << 6;

enum class EventType : uint32_t {
	MouseMove,
	MouseDown,   // a = button (0 left, 1 middle, 2 right), b = 1 if it came from the pad
	MouseUp,     // a = button
	Wheel,       // a = delta X, b = delta Y
	KeyDown,     // a = virtual key, b = lParam
	KeyUp,       // a = virtual key, b = lParam
	Char,        // a = UTF-16 character, b = lParam
	Show,
	Hide,
	Invalidate,  // asks for a repaint, for example after losing the D3D device
	MailChanged, // a = unread emails
	Quit,
	PadKeyboard  // a = PadKeyboardAction
};

// What the pad asks of the on-screen keyboard.
enum class PadKeyboardAction : int32_t {
	Up, Down, Left, Right, Press, Close, Backspace, Space, CaretLeft, CaretRight, Shift, Go
};

struct Event {
	EventType type;
	int32_t x;
	int32_t y;
	int32_t a;
	int32_t b;
	uint32_t modifiers;
};
static_assert(sizeof(Event) == 24);

// What the player does in the inbox. Emails belong to the plugin: the helper only reads them.
enum class MailOpKind : uint32_t { MarkRead, Remove, ReplyGood, ReplyBad };

struct MailOp {
	MailOpKind kind;
	uint32_t id;
};
static_assert(sizeof(MailOp) == 8);

// A value a page wants to store. The keys and texts come from mods and pages.
struct ValueOp {
	char key[kValueKeySize];
	char value[kValueTextSize];
};

// What pages can know about the game. The plugin publishes it several times a second.
struct GameStateData {
	int32_t inGame;
	int32_t money;
	float health;
	float armor;
	int32_t wantedLevel;
	int32_t day;
	int32_t hour;
	int32_t minute;
	float x;
	float y;
	float z;
	int32_t interior;
	int32_t inVehicle;
	int32_t onMission;
	int32_t weather;
	char zone[64];
	char lastMission[16];
};

enum class HelperState : uint32_t { Starting, Ready, Failed };
enum class CursorKind : uint32_t { Arrow, Hand, IBeam };

struct Header {
	uint32_t magic;
	uint32_t protocol;
	uint32_t width;
	uint32_t height;
	uint32_t stride;

	// Triple buffer: the helper writes to its slot and swaps it with "ready",
	// and the plugin does the same to read. Nobody ever waits for anybody.
	std::atomic<uint32_t> ready;
	std::atomic<uint32_t> state;
	std::atomic<uint32_t> cursor;
	std::atomic<uint32_t> paintCount;
	std::atomic<uint32_t> closeRequest;  // The interface asks to return to the game (Log Off).

	// Event ring with a single producer (plugin) and a single consumer (helper).
	std::atomic<uint32_t> ringWrite;
	std::atomic<uint32_t> ringRead;
	Event ring[kRingSize];

	// The same idea in reverse: a single producer (helper) and a single consumer (plugin).
	std::atomic<uint32_t> mailOpWrite;
	std::atomic<uint32_t> mailOpRead;
	MailOp mailOps[kMailOpRingSize];

	// CLEO scripts requested by a local site, as "domain/name". The helper has already checked them.
	std::atomic<uint32_t> siteScriptWrite;
	std::atomic<uint32_t> siteScriptRead;
	char siteScripts[kSiteScriptRingSize][kSiteScriptNameSize];

	std::atomic<uint32_t> valueOpWrite;
	std::atomic<uint32_t> valueOpRead;
	ValueOp valueOps[kValueOpRingSize];

	// Read with a counter: odd while the plugin writes, and if it changes, read it again.
	std::atomic<uint32_t> gameStateSequence;
	GameStateData gameState;

	// The on-screen keyboard is open: the pad belongs to it, not to the cursor or pager.
	std::atomic<uint32_t> keyboardOpen;
};
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(sizeof(Header) <= kHeaderSize);
static_assert(offsetof(Header, ring) == 48);

inline size_t SectionSize(uint32_t stride, uint32_t height) {
	return size_t(kHeaderSize) + size_t(stride) * height * kSlots;
}

inline uint8_t* SlotPixels(Header* header, uint32_t slot) {
	return reinterpret_cast<uint8_t*>(header) + kHeaderSize + size_t(header->stride) * header->height * slot;
}

}  // namespace ef
