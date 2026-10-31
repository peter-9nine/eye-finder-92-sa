#pragma once
// Las teclas que se pueden cambiar, en EyeFinder92SA.ini. No depende del juego, así que el arnés también lo usa.
#include <string>

struct KeyConfig {
	unsigned int browser = 0x77;     // F8
	unsigned int pagerOpen = 0x75;   // F6
	unsigned int pagerOlder = 0x21;  // Page Up
	unsigned int pagerNewer = 0x22;  // Page Down
};

// Devuelve la tecla virtual, 0 para "None", o -1 si no se reconoce el nombre.
int ParseKeyName(const std::string& name);

// Crea el archivo con los valores por defecto si no existe. Lo que no se entienda se deja por
// defecto y se explica en problems: una errata en el archivo nunca debe impedir jugar.
KeyConfig LoadKeyConfig(const std::wstring& path, std::string& problems);
