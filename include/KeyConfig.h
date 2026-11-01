#pragma once
// The keys that can be changed, in EyeFinder92SA.ini. It doesn't depend on the game, so the harness uses it too.
#include <string>

struct KeyConfig {
	unsigned int browser = 0x77;     // F8
	unsigned int pagerOpen = 0x75;   // F6
	unsigned int pagerOlder = 0x21;  // Page Up
	unsigned int pagerNewer = 0x22;  // Page Down
};

// Returns the virtual key, 0 for "None", or -1 if the name isn't recognized.
int ParseKeyName(const std::string& name);

// Creates the file with the default values if it's missing. Anything not understood keeps its
// default and is explained in problems: a typo in the file must never stop anyone playing.
KeyConfig LoadKeyConfig(const std::wstring& path, std::string& problems);
