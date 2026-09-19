#include "install.h"
#include "settings.h"
#include <fstream>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace perf
{
static std::filesystem::path known(REFKNOWNFOLDERID id)
{
    PWSTR value = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &value)))
        throw std::runtime_error("Known folder unavailable");
    std::filesystem::path result(value);
    CoTaskMemFree(value);
    return result;
}
InstallPaths userInstallPaths()
{
    return {known(FOLDERID_LocalAppData) / L"Programs" / L"NativePerfMonitor-1.6", known(FOLDERID_Desktop),
            known(FOLDERID_Programs),
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NativePerfMonitor-1.6"};
}
std::filesystem::path shortcutTarget(const std::filesystem::path &shortcut)
{
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&file)) || FAILED(file->Load(shortcut.c_str(), STGM_READ)))
        return {};
    wchar_t path[32768]{};
    if (FAILED(link->GetPath(path, 32768, nullptr, SLGP_RAWPATH)))
        return {};
    return path;
}
static bool equalPath(const std::filesystem::path &a, const std::filesystem::path &b)
{
    return _wcsicmp(a.lexically_normal().c_str(), b.lexically_normal().c_str()) == 0;
}
static std::wstring read(HKEY key, const wchar_t *name)
{
    wchar_t data[32768]{};
    DWORD bytes = sizeof(data);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, data, &bytes) == ERROR_SUCCESS ? data
                                                                                                   : L"";
}
static void set(HKEY key, const wchar_t *name, const std::wstring &value)
{
    if (RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                       DWORD((value.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
        throw std::runtime_error("Could not write Installed Apps entry");
}
static void setNumber(HKEY key, const wchar_t *name, DWORD value)
{
    if (RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value)) !=
        ERROR_SUCCESS)
        throw std::runtime_error("Could not write Installed Apps entry");
}
bool installPackage(const std::filesystem::path &source, const InstallPaths &paths, bool autostart,
                    std::wstring &error)
{
    const std::vector<std::wstring> names = {L"PerfMonitor.exe",      L"Uninstall.exe", L"Setup.exe",
                                             L"ReadMe.txt",           L"LICENSE.txt",   L"SHA256SUMS.txt",
                                             L"package-manifest.json"};
    std::vector<std::filesystem::path> created;
    HKEY key = nullptr;
    bool createdKey = false, createdFolder = false, startupAdded = false;
    try
    {
        if (!source.is_absolute() || !paths.destination.is_absolute() || !paths.desktop.is_absolute() ||
            !paths.programs.is_absolute())
            throw std::runtime_error("Installation paths must be absolute");
        for (auto &path : {source, paths.destination, paths.desktop, paths.programs})
            if (hasReparseAncestor(path))
                throw std::runtime_error("A redirected installation path was refused");
        std::ifstream marker(source / L"package-manifest.json");
        std::string content((std::istreambuf_iterator<char>(marker)), {});
        if (content.find("NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2-v1.6") == std::string::npos)
            throw std::runtime_error("Extract the complete version 1.6 package before installing");
        for (auto &name : names)
        {
            auto file = source / name;
            if (!std::filesystem::is_regular_file(file) || hasReparseAncestor(file))
                throw std::runtime_error("A required package file is missing or redirected");
        }
        auto target = paths.destination / L"PerfMonitor.exe";
        std::vector<std::filesystem::path> shortcuts = {paths.desktop / L"Native Performance Monitor 1.6.lnk",
                                                        paths.programs /
                                                            L"Native Performance Monitor 1.6.lnk"};
        for (auto &path : shortcuts)
        {
            if (hasReparseAncestor(path))
                throw std::runtime_error("A redirected shortcut was refused");
            if (std::filesystem::exists(path) && !equalPath(shortcutTarget(path), target))
                throw std::runtime_error("An unrelated shortcut uses this name; nothing was replaced");
        }
        if (RegOpenKeyExW(HKEY_CURRENT_USER, paths.registryKey.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            bool owned = read(key, L"OwnerId") == applicationId &&
                         equalPath(read(key, L"InstallLocation"), paths.destination);
            RegCloseKey(key);
            key = nullptr;
            if (!owned)
                throw std::runtime_error("Another installation owns the Installed Apps entry");
        }
        if (std::filesystem::exists(paths.destination))
        {
            if (!std::filesystem::is_directory(paths.destination))
                throw std::runtime_error("The destination is not a folder");
            if (!equalPath(source, paths.destination))
            {
                for (auto &name : names)
                    if (std::filesystem::exists(paths.destination / name))
                        throw std::runtime_error(
                            "Version 1.6 is already installed here. Remove that copy before reinstalling");
            }
        }
        else
        {
            std::filesystem::create_directories(paths.destination);
            createdFolder = true;
        }
        if (!equalPath(source, paths.destination))
            for (auto &name : names)
            {
                auto destination = paths.destination / name;
                std::filesystem::copy_file(source / name, destination);
                created.push_back(destination);
                SetFileAttributesW(destination.c_str(), FILE_ATTRIBUTE_NORMAL);
            }
        for (auto &path : shortcuts)
            if (!std::filesystem::exists(path))
            {
                ComPtr<IShellLinkW> link;
                ComPtr<IPersistFile> persist;
                if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(&link))) ||
                    FAILED(link.As(&persist)))
                    throw std::runtime_error("Could not create the desktop shortcut");
                link->SetPath(target.c_str());
                link->SetWorkingDirectory(paths.destination.c_str());
                link->SetDescription(L"Native Performance Monitor");
                link->SetIconLocation(target.c_str(), 0);
                link->SetShowCmd(SW_SHOWNOACTIVATE);
                if (FAILED(persist->Save(path.c_str(), TRUE)))
                    throw std::runtime_error("Could not save the desktop or Start menu shortcut");
                created.push_back(path);
            }
        DWORD disposition = 0;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, paths.registryKey.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                            nullptr, &key, &disposition) != ERROR_SUCCESS)
            throw std::runtime_error("Could not register the application in Installed Apps");
        createdKey = disposition == REG_CREATED_NEW_KEY;
        set(key, L"OwnerId", applicationId);
        set(key, L"DisplayName", L"Native Performance Monitor 1.6");
        set(key, L"DisplayVersion", displayVersion);
        set(key, L"Publisher", L"NativePerfMonitor Project");
        set(key, L"InstallLocation", paths.destination.wstring());
        set(key, L"UninstallString", L"\"" + (paths.destination / L"Uninstall.exe").wstring() + L"\"");
        set(key, L"DisplayIcon", L"\"" + target.wstring() + L"\",0");
        setNumber(key, L"NoModify", 1);
        setNumber(key, L"NoRepair", 1);
        uintmax_t bytes = 0;
        for (auto &name : names)
            bytes += std::filesystem::file_size(paths.destination / name);
        setNumber(key, L"EstimatedSize", DWORD((bytes + 1023) / 1024));
        RegCloseKey(key);
        key = nullptr;
        if (autostart && !startupEnabled(target))
        {
            if (!setStartup(target, true, error))
                throw std::runtime_error(
                    "Installation could not enable optional startup; no files were kept");
            startupAdded = true;
        }
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATHW, paths.desktop.c_str(), nullptr);
        return true;
    }
    catch (const std::exception &e)
    {
        if (key)
            RegCloseKey(key);
        if (startupAdded)
        {
            std::wstring ignored;
            setStartup(paths.destination / L"PerfMonitor.exe", false, ignored);
        }
        if (createdKey)
            RegDeleteKeyW(HKEY_CURRENT_USER, paths.registryKey.c_str());
        std::error_code ec;
        for (auto i = created.rbegin(); i != created.rend(); ++i)
            std::filesystem::remove(*i, ec);
        if (createdFolder)
            std::filesystem::remove(paths.destination, ec);
        std::string message = e.what();
        error.assign(message.begin(), message.end());
        return false;
    }
}
} // namespace perf
