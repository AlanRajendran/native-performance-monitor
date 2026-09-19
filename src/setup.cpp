#include "install.h"
#include "settings.h"
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR arguments, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try
    {
        auto paths = perf::userInstallPaths();
        std::wstring message =
            L"Install for your Windows account in:\n" + paths.destination.wstring() +
            L"\n\nAdds a desktop shortcut, a Start menu shortcut, and an Installed Apps uninstall entry. No "
            L"administrator access is needed. Earlier versions remain unchanged.";
        TASKDIALOG_BUTTON buttons[] = {{IDYES, L"Install"}, {IDCANCEL, L"Cancel"}};
        TASKDIALOGCONFIG config{sizeof(config)};
        config.hInstance = h;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
        config.pszWindowTitle = L"Install Native Performance Monitor 1.5";
        config.pszMainInstruction = L"Set up Native Performance Monitor";
        config.pszContent = message.c_str();
        config.pszVerificationText = L"Start automatically when I sign in to Windows";
        config.cButtons = 2;
        config.pButtons = buttons;
        config.nDefaultButton = IDYES;
        int clicked = 0;
        BOOL startup = FALSE;
        bool unattended = std::wstring(arguments) == L"--install-no-launch";
        if (*arguments && !unattended)
        {
            CoUninitialize();
            return 2;
        }
        if (unattended ||
            (SUCCEEDED(TaskDialogIndirect(&config, &clicked, nullptr, &startup)) && clicked == IDYES))
        {
            std::wstring error;
            if (perf::installPackage(perf::executablePath().parent_path(), paths, startup != FALSE, error))
            {
                if (unattended)
                {
                    CoUninitialize();
                    return 0;
                }
                auto portable = perf::executablePath().parent_path() / L"PerfMonitor.exe";
                auto running = FindWindowW(L"NativePerfMonitor.Controller.1.5", nullptr);
                if (running)
                {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(running, &pid);
                    auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
                    if (process)
                    {
                        wchar_t path[32768]{};
                        DWORD length = 32768;
                        if (QueryFullProcessImageNameW(process, 0, path, &length) &&
                            _wcsicmp(path, portable.c_str()) == 0)
                        {
                            PostMessageW(running,
                                         RegisterWindowMessageW(L"NativePerfMonitor.Stop.6D845648.v1.5"), 0,
                                         0);
                            WaitForSingleObject(process, 5000);
                        }
                        CloseHandle(process);
                    }
                }
                auto exe = paths.destination / L"PerfMonitor.exe";
                ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, paths.destination.c_str(),
                              SW_SHOWNOACTIVATE);
                MessageBoxW(
                    nullptr,
                    L"Installed. Use the desktop shortcut to open the monitor, or its notification-area icon "
                    L"for controls. You can remove it through Windows Installed Apps.",
                    L"Native Performance Monitor", MB_OK | MB_ICONINFORMATION);
                result = 0;
            }
            else if (!unattended)
                MessageBoxW(nullptr, error.c_str(), L"Installation did not complete", MB_OK | MB_ICONERROR);
        }
        else
            result = 0;
    }
    catch (...)
    {
        MessageBoxW(nullptr, L"Windows could not resolve the per-user installation folders.", L"Install",
                    MB_OK | MB_ICONERROR);
    }
    CoUninitialize();
    return result;
}
