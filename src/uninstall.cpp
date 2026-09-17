#include <windows.h>

#include <filesystem>
#include <shellapi.h>
#include <string>
#include <vector>
#include <wincrypt.h>

static std::wstring literal(const std::wstring &value)
{
    std::wstring s = L"'";
    for (auto c : value)
    {
        s += c;
        if (c == L'\'')
            s += c;
    }
    return s + L"'";
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    std::wstring module(32768, L'\0');
    auto length = GetModuleFileNameW(nullptr, module.data(), DWORD(module.size()));
    if (!length || length >= module.size())
        return 2;
    module.resize(length);
    auto folder = std::filesystem::path(module).parent_path();
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    bool isolated = false, yes = false;
    std::wstring data, result, integrationRoot;
    for (int i = 1; i < count; ++i)
    {
        std::wstring arg = argv[i];
        if (arg == L"--isolated")
            isolated = true;
        else if (arg == L"--yes")
            yes = true;
        else if (arg == L"--data-dir" && i + 1 < count)
            data = argv[++i];
        else if (arg == L"--integration-test-root" && i + 1 < count)
            integrationRoot = argv[++i];
        else if (arg == L"--result" && i + 1 < count)
            result = argv[++i];
        else
        {
            LocalFree(argv);
            return 2;
        }
    }
    LocalFree(argv);
    if ((yes || !data.empty() || !result.empty() || !integrationRoot.empty()) &&
        (!isolated || data.empty() || result.empty()))
        return 2;
    if (!yes)
    {
        auto message = L"Remove Native Performance Monitor 1.3 from:\n\n" + folder.wstring() +
                       L"\n\nThis removes this copy's program files, settings and matching optional startup "
                       L"entry, owned shortcuts and Installed Apps registration. Earlier versions and "
                       L"unrelated files are kept.";
        if (MessageBoxW(nullptr, message.c_str(), L"Uninstall Native Performance Monitor 1.3",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return 0;
    }
    auto resource = FindResourceW(instance, MAKEINTRESOURCEW(101), RT_RCDATA);
    auto loaded = resource ? LoadResource(instance, resource) : nullptr;
    auto bytes = resource ? SizeofResource(instance, resource) : 0;
    auto utf8 = loaded ? static_cast<const char *>(LockResource(loaded)) : nullptr;
    if (!utf8 || !bytes)
        return 3;
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, int(bytes), nullptr, 0);
    if (!needed)
        return 3;
    std::wstring script(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, int(bytes), script.data(), needed);
    script = L"& {\n" + script + L"\n} -Yes -PackageDirectory " + literal(folder.wstring()) +
             L" -ParentPid " + std::to_wstring(GetCurrentProcessId());
    if (isolated)
        script += L" -Isolated -Quiet -DataDirectory " + literal(data) + L" -ResultFile " + literal(result);
    if (!integrationRoot.empty())
        script += L" -IntegrationTestRoot " + literal(integrationRoot);
    DWORD encodedSize = 0;
    CryptBinaryToStringW(reinterpret_cast<const BYTE *>(script.data()), DWORD(script.size() * 2),
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &encodedSize);
    std::wstring encoded(encodedSize, L'\0');
    if (!CryptBinaryToStringW(reinterpret_cast<const BYTE *>(script.data()), DWORD(script.size() * 2),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &encodedSize))
        return 3;
    if (!encoded.empty() && encoded.back() == 0)
        encoded.pop_back();
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH))
        return 3;
    auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
    std::wstring command = L"\"" + powershell.wstring() +
                           L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " +
                           encoded;
    if (command.size() >= 32767)
        return 3;
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, system, &startup, &process))
    {
        if (!yes)
            MessageBoxW(nullptr,
                        L"Could not start the built-in Windows cleanup process. Nothing was removed.",
                        L"Uninstall", MB_ICONERROR);
        return 4;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    // The embedded cleanup waits for this launcher to exit before deleting both EXEs.
    return 0;
}
