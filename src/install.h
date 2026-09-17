#pragma once
#include <filesystem>
#include <string>
namespace perf
{
struct InstallPaths
{
    std::filesystem::path destination, desktop, programs;
    std::wstring registryKey;
};
InstallPaths userInstallPaths();
std::filesystem::path shortcutTarget(const std::filesystem::path &shortcut);
bool installPackage(const std::filesystem::path &source, const InstallPaths &paths, bool autostart,
                    std::wstring &error);
} // namespace perf
