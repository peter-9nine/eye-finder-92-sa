#pragma once
// The computers the player can use, from locations.json. Each one's type decides what its session looks like.
// It doesn't depend on the game, so the harness uses it too.
#include <cstdint>
#include <string>
#include <vector>

#include "ipc.h"

struct ComputerLocation {
	std::string name;
	ef::SessionType type = ef::SessionType::Normal;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float radius = 3.0f;
	int interior = -1;  // -1 is any: interiors share coordinates, so a location can name its own.
	int pricePerMinute = 0;  // Only normal sessions are charged.
};

// A bad entry is skipped and explained in problems. With no usable entries, the cyber cafe is
// used instead, because a typo in the file must never take away every computer in the game.
std::vector<ComputerLocation> LoadLocations(const std::wstring& path, std::string& problems);

const ComputerLocation* FindComputer(const std::vector<ComputerLocation>& locations, float x, float y, float z, int interior);

// Every started minute is paid in full, and a session always costs at least one.
int SessionCost(int seconds, int pricePerMinute);
