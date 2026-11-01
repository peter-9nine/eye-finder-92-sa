#pragma once
// Recognizes misspelled EyeFinder addresses, for an easter egg. It has no dependencies, so the harness can test it.
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ef {

// How many letters must be added, removed, changed or swapped with a neighbor to turn one word into another.
inline int SpellingDistance(const std::string& a, const std::string& b) {
	std::vector<std::vector<int>> cost(a.size() + 1, std::vector<int>(b.size() + 1));
	for (size_t i = 0; i <= a.size(); ++i) cost[i][0] = static_cast<int>(i);
	for (size_t j = 0; j <= b.size(); ++j) cost[0][j] = static_cast<int>(j);

	for (size_t i = 1; i <= a.size(); ++i) {
		for (size_t j = 1; j <= b.size(); ++j) {
			const int change = a[i - 1] == b[j - 1] ? 0 : 1;
			cost[i][j] = (std::min)({ cost[i - 1][j] + 1, cost[i][j - 1] + 1, cost[i - 1][j - 1] + change });
			if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) cost[i][j] = (std::min)(cost[i][j], cost[i - 2][j - 2] + 1);
		}
	}
	return cost[a.size()][b.size()];
}

// True for a host that's almost www.eyefinder.info, but isn't it.
inline bool IsEyeFinderTypo(std::string host) {
	for (char& c : host) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	// "www." is optional, and "ww." or "wwww." are mistakes too.
	bool exactWww = false;
	const size_t firstDot = host.find('.');
	if (firstDot != std::string::npos && firstDot >= 2 && firstDot <= 4 && host.find_first_not_of('w') == firstDot) {
		exactWww = firstDot == 3;
		host.erase(0, firstDot + 1);
	}

	// Exactly a name and an ending, so that a real subdomain such as mail.eyefinder.info is left alone.
	const size_t dot = host.find('.');
	if (dot == std::string::npos || dot == 0 || host.find('.', dot + 1) != std::string::npos) return false;
	const std::string name = host.substr(0, dot);
	const std::string ending = host.substr(dot + 1);
	if (ending.empty()) return false;

	if (name == "eyefinder" && ending == "info" && exactWww) return false;

	const int nameDistance = SpellingDistance(name, "eyefinder");
	const int endingDistance = SpellingDistance(ending, "info");
	const bool commonEnding = ending == "com" || ending == "net" || ending == "org";

	if (nameDistance == 0) return endingDistance <= 2 || commonEnding;

	// Stricter on common endings, which are full of real sites. At two letters, eyeliner.com would match.
	if (commonEnding) return nameDistance == 1;
	return nameDistance <= 2 && endingDistance <= 1;
}

}  // namespace ef
