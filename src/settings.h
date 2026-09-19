#pragma once
#include "core.h"
#include <filesystem>
#include <windows.h>
namespace perf
{
inline constexpr wchar_t applicationId[] = L"NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2-v2.0";
struct Settings
{
    bool panel = true, strip = true, locked = true, compact = false;
    bool insideTaskbar = true;
    // Loads NVIDIA's management library (about 20 MB) for the memory bus row.
    bool memoryBus = false;
    Range range = Range::Seconds;
    int panelX = -1, panelY = -1, panelW = 450, panelH = 1340, stripX = -1;
    int opacity = 25;
    uint64_t adapter = 0;
};
std::filesystem::path executablePath();
std::filesystem::path defaultDataDirectory();
Settings loadSettings(const std::filesystem::path &directory);
// Copies the settings of the most recent earlier release line, if one exists
// beside `directory`, into `out`. Returns whether anything was imported.
bool importPreviousSettings(const std::filesystem::path &directory, Settings &out);
bool saveSettings(const std::filesystem::path &directory, const Settings &settings, std::wstring &error);
bool hasReparseAncestor(const std::filesystem::path &path);
std::wstring startupCommand(const std::filesystem::path &exe);
bool startupEnabled(const std::filesystem::path &exe);
bool setStartup(const std::filesystem::path &exe, bool enabled, std::wstring &error);
bool prepareUninstall(const std::filesystem::path &exe, const std::filesystem::path &data, bool isolated,
                      std::wstring &error);
} // namespace perf
