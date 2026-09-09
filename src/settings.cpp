#include "settings.h"
#include <algorithm>
#include <fstream>
#include <shlobj.h>
#include <sstream>

namespace perf
{
std::filesystem::path executablePath()
{
    std::wstring s(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, s.data(), DWORD(s.size()));
    s.resize(n);
    return s;
}
std::filesystem::path defaultDataDirectory()
{
    PWSTR p = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
    {
        out = p;
        CoTaskMemFree(p);
    }
    return out / L"NativePerfMonitor";
}
bool hasReparseAncestor(const std::filesystem::path &value)
{
    std::error_code ec;
    auto p = std::filesystem::absolute(value, ec).lexically_normal();
    if (ec)
        return true;
    for (; !p.empty();)
    {
        auto a = GetFileAttributesW(p.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT))
            return true;
        auto up = p.parent_path();
        if (up == p)
            break;
        p = up;
    }
    return false;
}
static bool ownedData(const std::filesystem::path &dir)
{
    auto marker = dir / L".nativeperf-settings";
    if (hasReparseAncestor(marker))
        return false;
    std::ifstream f(marker);
    std::string s;
    std::getline(f, s);
    return s == "NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2";
}
Settings loadSettings(const std::filesystem::path &dir)
{
    Settings r;
    if (hasReparseAncestor(dir) || !ownedData(dir))
        return r;
    std::error_code ec;
    if (std::filesystem::file_size(dir / L"settings.ini", ec) > 8192 || ec)
        return r;
    std::ifstream f(dir / L"settings.ini");
    std::string line;
    while (std::getline(f, line))
    {
        auto p = line.find('=');
        if (p == line.npos)
            continue;
        auto k = line.substr(0, p), v = line.substr(p + 1);
        try
        {
            size_t used = 0;
            auto n = std::stoll(v, &used);
            if (used != v.size())
                continue;
            if (k == "panel")
                r.panel = n != 0;
            else if (k == "strip")
                r.strip = n != 0;
            else if (k == "locked")
                r.locked = n != 0;
            else if (k == "compact")
                r.compact = n != 0;
            else if (k == "insideTaskbar")
                r.insideTaskbar = n != 0;
            else if (k == "panelX")
                r.panelX = int(std::clamp(n, -1LL, 100000LL));
            else if (k == "panelY")
                r.panelY = int(std::clamp(n, -1LL, 100000LL));
            else if (k == "panelW")
                r.panelW = int(std::clamp(n, 300LL, 900LL));
            else if (k == "panelH")
                r.panelH = int(std::clamp(n, 260LL, 1200LL));
            else if (k == "stripX")
                r.stripX = int(std::clamp(n, -1LL, 100000LL));
        }
        catch (...)
        {
        }
        if (k == "adapter")
        {
            try
            {
                size_t used = 0;
                auto n = std::stoull(v, &used);
                if (used == v.size())
                    r.adapter = n;
            }
            catch (...)
            {
            }
        }
    }
    return r;
}
bool saveSettings(const std::filesystem::path &dir, const Settings &s, std::wstring &error)
{
    if (dir.empty() || hasReparseAncestor(dir))
    {
        error = L"Settings path is unavailable or contains a reparse point.";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
    {
        error = L"Cannot create the settings directory.";
        return false;
    }
    auto marker = dir / L".nativeperf-settings";
    if (std::filesystem::exists(marker) && !ownedData(dir))
    {
        error = L"Settings directory belongs to another application.";
        return false;
    }
    if (!std::filesystem::exists(marker))
    {
        std::ofstream f(marker);
        f << "NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2\n";
        if (!f)
        {
            error = L"Cannot write settings marker.";
            return false;
        }
    }
    const auto target = dir / L"settings.ini", temp = dir / L"settings.ini.new";
    if (hasReparseAncestor(target) || hasReparseAncestor(temp))
    {
        error = L"Refusing redirected settings file.";
        return false;
    }
    {
        std::ofstream f(temp, std::ios::trunc);
        f << "version=1\n"
          << "panel=" << s.panel << "\nstrip=" << s.strip << "\nlocked=" << s.locked
          << "\ncompact=" << s.compact << "\npanelX=" << s.panelX << "\npanelY=" << s.panelY
          << "\npanelW=" << s.panelW << "\npanelH=" << s.panelH << "\nstripX=" << s.stripX
          << "\nadapter=" << s.adapter << "\ninsideTaskbar=" << s.insideTaskbar << "\n";
        if (!f)
        {
            error = L"Cannot write settings.";
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = L"Cannot replace settings file.";
        return false;
    }
    return true;
}
static constexpr wchar_t runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
std::wstring startupCommand(const std::filesystem::path &exe)
{
    auto s = exe.wstring();
    if (s.find(L'"') != s.npos || s.empty() || !exe.is_absolute())
        return {};
    auto c = L"\"" + s + L"\" --autostart";
    return c.size() <= 260 ? c : L"";
}
static std::wstring readStartup()
{
    wchar_t value[2048]{};
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, runKey, L"NativePerfMonitor", RRF_RT_REG_SZ, nullptr, value,
                     &bytes) != ERROR_SUCCESS)
        return {};
    return value;
}
bool startupEnabled(const std::filesystem::path &exe)
{
    auto expected = startupCommand(exe);
    return !expected.empty() && _wcsicmp(readStartup().c_str(), expected.c_str()) == 0;
}
bool setStartup(const std::filesystem::path &exe, bool enabled, std::wstring &error)
{
    auto expected = startupCommand(exe);
    auto old = readStartup();
    if (enabled && expected.empty())
    {
        error = L"The executable path is too long for Windows autostart.";
        return false;
    }
    if (!old.empty() && _wcsicmp(old.c_str(), expected.c_str()) != 0)
    {
        error = L"Another installation owns the NativePerfMonitor startup entry.";
        return false;
    }
    if (!enabled && old.empty())
        return true;
    HKEY key = nullptr;
    auto st =
        RegCreateKeyExW(HKEY_CURRENT_USER, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (st == ERROR_SUCCESS)
    {
        st = enabled ? RegSetValueExW(key, L"NativePerfMonitor", 0, REG_SZ,
                                      reinterpret_cast<const BYTE *>(expected.c_str()),
                                      DWORD((expected.size() + 1) * sizeof(wchar_t)))
                     : RegDeleteValueW(key, L"NativePerfMonitor");
        RegCloseKey(key);
    }
    if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND)
    {
        error = L"Windows could not update the per-user startup entry.";
        return false;
    }
    return true;
}
bool prepareUninstall(const std::filesystem::path &exe, const std::filesystem::path &data, bool isolated,
                      std::wstring &error)
{
    if (hasReparseAncestor(data))
    {
        error = L"Refusing a redirected settings directory.";
        return false;
    }
    if (!isolated && startupEnabled(exe) && !setStartup(exe, false, error))
        return false;
    if (!std::filesystem::exists(data))
        return true;
    if (!ownedData(data))
    {
        error = L"Settings ownership marker is missing; settings were left in place.";
        return false;
    }
    for (const wchar_t *name : {L"settings.ini", L"settings.ini.new", L"diagnostics.txt"})
    {
        auto p = data / name;
        if (hasReparseAncestor(p))
        {
            error = L"Refusing a redirected settings file.";
            return false;
        }
    }
    std::error_code ec;
    for (const wchar_t *name :
         {L"settings.ini", L"settings.ini.new", L"diagnostics.txt", L".nativeperf-settings"})
    {
        std::filesystem::remove(data / name, ec);
        if (ec)
        {
            error = L"Could not remove an application settings file.";
            return false;
        }
    }
    if (std::filesystem::is_empty(data, ec))
        std::filesystem::remove(data, ec);
    return !ec;
}
} // namespace perf
