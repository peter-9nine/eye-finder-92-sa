#pragma once
// Protocolo de memoria compartida entre el plugin (32 bits) y el helper (64 bits).
// Solo tipos de tamaño fijo: la disposición debe ser idéntica en ambas arquitecturas.
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ef {

constexpr uint32_t kMagic = 0x32394645;  // "EF92"
constexpr uint32_t kProtocol = 2;
constexpr uint32_t kSlots = 3;
constexpr uint32_t kNewFrame = 0x80000000u;
constexpr uint32_t kRingSize = 256;
constexpr uint32_t kHeaderSize = 64 * 1024;

// Mismos valores que EVENTFLAG_* de CEF.
constexpr uint32_t kModShift = 1u << 1;
constexpr uint32_t kModControl = 1u << 2;
constexpr uint32_t kModAlt = 1u << 3;
constexpr uint32_t kModLeftMouse = 1u << 4;
constexpr uint32_t kModMiddleMouse = 1u << 5;
constexpr uint32_t kModRightMouse = 1u << 6;

enum class EventType : uint32_t {
	MouseMove,
	MouseDown,   // a = botón (0 izquierdo, 1 central, 2 derecho)
	MouseUp,     // a = botón
	Wheel,       // a = delta X, b = delta Y
	KeyDown,     // a = virtual key, b = lParam
	KeyUp,       // a = virtual key, b = lParam
	Char,        // a = carácter UTF-16, b = lParam
	Show,
	Hide,
	Invalidate,  // pide repintar, por ejemplo tras perder el dispositivo D3D
	Quit
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

enum class HelperState : uint32_t { Starting, Ready, Failed };
enum class CursorKind : uint32_t { Arrow, Hand, IBeam };

struct Header {
	uint32_t magic;
	uint32_t protocol;
	uint32_t width;
	uint32_t height;
	uint32_t stride;

	// Triple búfer: el helper escribe en su slot y lo intercambia con "ready";
	// el plugin hace lo mismo para leer. Nadie espera nunca a nadie.
	std::atomic<uint32_t> ready;
	std::atomic<uint32_t> state;
	std::atomic<uint32_t> cursor;
	std::atomic<uint32_t> paintCount;
	std::atomic<uint32_t> closeRequest;  // La interfaz pide volver al juego (Log Off).

	// Cola de eventos con un solo productor (plugin) y un solo consumidor (helper).
	std::atomic<uint32_t> ringWrite;
	std::atomic<uint32_t> ringRead;
	Event ring[kRingSize];
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
