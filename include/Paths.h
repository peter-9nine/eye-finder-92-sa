#pragma once
#include <string>

std::wstring PluginDirectory();
std::wstring SaveDataDirectory();
std::wstring SessionSavePath();
std::wstring FileUrl(const std::wstring& path);
void Log(const std::string& line);
