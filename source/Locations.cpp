#include "Locations.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

constexpr int kDefaultPricePerMinute = 10;

ComputerLocation CyberCafe() {
	ComputerLocation cafe;
	cafe.name = "Cyber cafe";
	cafe.x = 1170.4697f;
	cafe.y = -1655.1893f;
	cafe.z = 1014.3158f;
	cafe.radius = 3.0f;
	cafe.pricePerMinute = kDefaultPricePerMinute;
	return cafe;
}

bool ParseLocation(const json& item, ComputerLocation& location, std::string& why) {
	if (!item.is_object()) {
		why = "it isn't an object";
		return false;
	}

	const auto position = item.find("position");
	if (position == item.end() || !position->is_array() || position->size() != 3 ||
		!(*position)[0].is_number() || !(*position)[1].is_number() || !(*position)[2].is_number()) {
		why = "position must be [x, y, z]";
		return false;
	}
	location.x = (*position)[0].get<float>();
	location.y = (*position)[1].get<float>();
	location.z = (*position)[2].get<float>();

	const std::string type = item.value("type", std::string());
	if (type == "normal") location.type = ef::SessionType::Normal;
	else if (type == "pd") location.type = ef::SessionType::Police;
	else if (type == "dark_web") location.type = ef::SessionType::DarkWeb;
	else {
		why = "type must be normal, pd or dark_web";
		return false;
	}

	location.name = item.value("name", std::string("Computer"));
	location.radius = item.value("radius", 3.0f);
	location.interior = item.value("interior", -1);
	if (location.radius <= 0.0f) {
		why = "radius must be above 0";
		return false;
	}

	if (location.type == ef::SessionType::Normal) {
		location.pricePerMinute = item.value("pricePerMinute", kDefaultPricePerMinute);
		if (location.pricePerMinute < 0) {
			why = "pricePerMinute can't be negative";
			return false;
		}
	}
	return true;
}

}  // namespace

std::vector<ComputerLocation> LoadLocations(const std::wstring& path, std::string& problems) {
	std::vector<ComputerLocation> locations;

	std::ifstream file{ std::filesystem::path(path) };
	const json root = file.is_open() ? json::parse(file, nullptr, false) : json(json::value_t::discarded);
	const auto list = root.is_object() ? root.find("locations") : root.end();

	if (!file.is_open()) {
		problems += "locations.json is missing. ";
	}
	else if (root.is_discarded()) {
		problems += "locations.json isn't valid JSON. ";
	}
	else if (!root.is_object() || list == root.end() || !list->is_array()) {
		problems += "locations.json needs a \"locations\" list. ";
	}
	else {
		int number = 0;
		for (const json& item : *list) {
			++number;
			ComputerLocation location;
			std::string why;
			// A wrong type in a field throws, and is treated like any other bad entry.
			try {
				if (ParseLocation(item, location, why)) locations.push_back(std::move(location));
				else problems += "locations.json entry " + std::to_string(number) + " skipped: " + why + ". ";
			}
			catch (const json::exception&) {
				problems += "locations.json entry " + std::to_string(number) + " skipped: a field has the wrong type. ";
			}
		}
	}

	if (locations.empty()) {
		problems += "Using only the cyber cafe. ";
		locations.push_back(CyberCafe());
	}
	return locations;
}

const ComputerLocation* FindComputer(const std::vector<ComputerLocation>& locations, float x, float y, float z, int interior) {
	for (const ComputerLocation& location : locations) {
		if (location.interior >= 0 && location.interior != interior) continue;
		const float dx = x - location.x;
		const float dy = y - location.y;
		const float dz = z - location.z;
		if (dx * dx + dy * dy + dz * dz < location.radius * location.radius) return &location;
	}
	return nullptr;
}

int SessionCost(int seconds, int pricePerMinute) {
	return pricePerMinute * (std::max)((seconds + 59) / 60, 1);
}
