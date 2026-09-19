#pragma once
#include <filesystem>
#include <string>
namespace perf
{
// The release version, reported in Windows Installed Apps, in diagnostics and
// in benchmark reports.
//
// Each release line is fully self contained, as 1.1, 1.2 and 1.3 were: its own
// install folder, settings directory, application id, window classes, single
// instance mutex, startup value, shortcuts and Installed Apps registration.
// Two lines can therefore be installed at once and neither can remove or
// corrupt the other. A new line imports the previous line's settings once, on
// first run, read-only (importPreviousSettings in settings.cpp).
//
// Releasing a new line means updating, together: this constant, the version
// resources under resources/, the $supportedVersions guard and the identity
// strings in packaging/Uninstall.ps1, the identity strings in src/, and the
// table of previous lines in importPreviousSettings.
#define NATIVE_PERF_VERSION "1.6.0"
inline constexpr wchar_t displayVersion[] = L"" NATIVE_PERF_VERSION;
inline constexpr char displayVersionNarrow[] = NATIVE_PERF_VERSION;
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
