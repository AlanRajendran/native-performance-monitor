#include "install.h"
#include "settings.h"
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <stdexcept>
using namespace perf;
static unsigned checks = 0;
static void require(bool ok, const char *message)
{
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
static std::wstring value(HKEY key, const wchar_t *name)
{
    wchar_t text[32768]{};
    DWORD n = sizeof(text);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, text, &n) == ERROR_SUCCESS ? text : L"";
}
static std::string readFile(const std::filesystem::path &p)
{
    std::ifstream f(p);
    return {(std::istreambuf_iterator<char>(f)), {}};
}
int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
        return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HKEY staging = nullptr;
    bool overridden = false;
    std::wstring testKey, startupTestKey;
    try
    {
        auto binary = std::filesystem::absolute(argv[1]);
        auto root = std::filesystem::absolute(argv[2]);
        GUID id{};
        CoCreateGuid(&id);
        wchar_t raw[64]{};
        StringFromGUID2(id, raw, 64);
        std::wstring guid;
        for (auto c : std::wstring(raw))
            if (iswxdigit(c))
                guid += wchar_t(towlower(c));
        root /= guid;
        auto source = root / L"source", data = root / L"settings";
        InstallPaths paths{root / L"installed", root / L"desktop", root / L"programs",
                           L"Software\\NativePerfMonitor-IntegrationTests\\" + guid};
        testKey = paths.registryKey;
        for (auto &p : {source, data, paths.desktop, paths.programs})
            std::filesystem::create_directories(p);
        for (auto n : {L"PerfMonitor.exe", L"Uninstall.exe", L"Setup.exe"})
            std::filesystem::copy_file(binary.parent_path() / n, source / n);
        for (auto n : {L"ReadMe.txt", L"LICENSE.txt", L"SHA256SUMS.txt"})
        {
            std::ofstream f(source / n);
            f << "fixture";
        }
        {
            std::ofstream f(source / L"package-manifest.json");
            f << "{\"owner\":\"NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2-v2.0\",\"version\":\"2."
                 "0.0\"}";
        }
        std::wstring error;
        require(installPackage(source, paths, false, error), "per-user install succeeds in isolated fixture");
        auto target = paths.destination / L"PerfMonitor.exe";
        require(std::filesystem::exists(target) &&
                    std::filesystem::exists(paths.destination / L"Uninstall.exe"),
                "both installed EXEs exist");
        require(shortcutTarget(paths.desktop / L"Native Performance Monitor 2.0.lnk") == target,
                "desktop shortcut resolves to installed EXE");
        require(shortcutTarget(paths.programs / L"Native Performance Monitor 2.0.lnk") == target,
                "Start menu shortcut resolves to installed EXE");
        HKEY key = nullptr;
        require(RegOpenKeyExW(HKEY_CURRENT_USER, testKey.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS,
                "Installed Apps registry entry exists");
        require(value(key, L"DisplayVersion") == displayVersion, "Installed Apps version");
        require(value(key, L"UninstallString") ==
                    L"\"" + (paths.destination / L"Uninstall.exe").wstring() + L"\"",
                "uninstall command quotes complete EXE path");
        require(value(key, L"OwnerId") == applicationId, "uninstall ownership marker");
        RegCloseKey(key);
        require(!installPackage(source, paths, false, error), "existing installation is not overwritten");
        require(std::filesystem::exists(target), "failed reinstall preserves current EXE");
        auto collision = paths;
        collision.destination = root / L"another";
        require(!installPackage(source, collision, false, error),
                "conflicting shortcut or registration rejected");
        require(!std::filesystem::exists(collision.destination),
                "failed ownership check creates no installation");
        auto sourceMissing = root / L"missing";
        std::filesystem::create_directory(sourceMissing);
        require(!installPackage(sourceMissing, collision, false, error), "incomplete package rejected");
        require(saveSettings(data, Settings{}, error), "create isolated owned settings");
        {
            std::ofstream f(paths.destination / L"unrelated.txt");
            f << "keep";
        }
        auto result = root / L"removal.json";
        auto uninstall = paths.destination / L"Uninstall.exe";
        std::wstring command = L"\"" + uninstall.wstring() + L"\" --isolated --yes --data-dir \"" +
                               data.wstring() + L"\" --result \"" + result.wstring() +
                               L"\" --integration-test-root \"" + root.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        require(CreateProcessW(uninstall.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, root.c_str(), &startup, &process),
                "launch actual uninstall EXE");
        CloseHandle(process.hThread);
        require(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0,
                "native uninstall launcher exits");
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess);
        require(code == 0, "native uninstall launcher succeeded");
        for (int i = 0; i < 200 && !std::filesystem::exists(result); ++i)
            Sleep(100);
        require(readFile(result).find("true") != std::string::npos, "cleanup worker reports success");
        require(!std::filesystem::exists(target) && !std::filesystem::exists(uninstall) &&
                    !std::filesystem::exists(paths.destination / L"Setup.exe"),
                "all three owned EXEs removed");
        require(!std::filesystem::exists(paths.desktop / L"Native Performance Monitor 2.0.lnk") &&
                    !std::filesystem::exists(paths.programs / L"Native Performance Monitor 2.0.lnk"),
                "both installed shortcuts removed");
        auto status = RegOpenKeyExW(HKEY_CURRENT_USER, testKey.c_str(), 0, KEY_READ, &key);
        if (status == ERROR_SUCCESS)
            RegCloseKey(key);
        require(status == ERROR_FILE_NOT_FOUND, "Installed Apps entry removed");
        require(!std::filesystem::exists(data), "owned settings removed");
        require(readFile(paths.destination / L"unrelated.txt") == "keep",
                "unrelated installed-folder file preserved");
        startupTestKey = L"Software\\NativePerfMonitor-StartupTests\\" + guid;
        require(RegCreateKeyExW(HKEY_CURRENT_USER, startupTestKey.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                                nullptr, &staging, nullptr) == ERROR_SUCCESS,
                "create isolated registry sandbox");
        require(RegOverridePredefKey(HKEY_CURRENT_USER, staging) == ERROR_SUCCESS,
                "redirect test registry operations");
        overridden = true;
        require(setStartup(target, true, error) && startupEnabled(target),
                "optional startup enables exact quoted executable");
        require(setStartup(target, false, error) && !startupEnabled(target),
                "disabling startup deletes the correct versioned value");
        require(setStartup(target, false, error), "disabling absent startup is idempotent");
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        overridden = false;
        RegCloseKey(staging);
        staging = nullptr;
        RegDeleteTreeW(HKEY_CURRENT_USER, startupTestKey.c_str());
        std::cout << checks << " installation assertions passed\n";
        CoUninitialize();
        return 0;
    }
    catch (const std::exception &e)
    {
        if (overridden)
            RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        if (staging)
            RegCloseKey(staging);
        if (!startupTestKey.empty())
            RegDeleteTreeW(HKEY_CURRENT_USER, startupTestKey.c_str());
        if (!testKey.empty())
            RegDeleteTreeW(HKEY_CURRENT_USER, testKey.c_str());
        std::cerr << "FAILED: " << e.what() << "\n";
        CoUninitialize();
        return 1;
    }
}
