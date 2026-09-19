#include "settings.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <shellapi.h>
#include <stdexcept>
#include <vector>
using namespace perf;
static unsigned checks = 0;
static void require(bool c, const char *text)
{
    ++checks;
    if (!c)
        throw std::runtime_error(text);
}
static std::wstring title(HWND h)
{
    wchar_t s[4096]{};
    GetWindowTextW(h, s, 4096);
    return s;
}
static void pump(unsigned ms)
{
    auto end = GetTickCount64() + ms;
    do
    {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(10);
    } while (GetTickCount64() < end);
}
static PROCESS_INFORMATION launch(const std::filesystem::path &exe, const std::filesystem::path &data)
{
    std::wstring command = L"\"" + exe.wstring() + L"\" --data-dir \"" + data.wstring() + L"\"";
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION p{};
    require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                           exe.parent_path().c_str(), &startup, &p),
            "launch isolated monitor");
    CloseHandle(p.hThread);
    return p;
}
int wmain(int argc, wchar_t **argv)
{
    if (argc < 3)
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PROCESS_INFORMATION process{};
    HWND target = nullptr;
    try
    {
        if (!GetShellWindow())
        {
            std::cout << "SKIP: interactive Explorer desktop required; sandbox desktop has no shell.\n";
            CoUninitialize();
            return 77;
        }
        require(!FindWindowW(L"NativePerfMonitor.Controller.1.6", nullptr),
                "no existing monitor; tests must not interrupt a user instance");
        auto exe = std::filesystem::absolute(argv[1]), root = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(root);
        std::wstring error;
        require(saveSettings(root / L"settings", Settings{}, error), "initialize isolated defaults");
        process = launch(exe, root / L"settings");
        HWND control = nullptr;
        for (int i = 0; i < 150; ++i)
        {
            control = FindWindowW(L"NativePerfMonitor.Controller.1.6", nullptr);
            if (control)
                break;
            pump(20);
        }
        require(control, "controller window created");
        pump(4500);
        struct Match
        {
            DWORD pid;
            std::vector<HWND> windows;
        } match{process.dwProcessId, {}};
        EnumWindows(
            [](HWND h, LPARAM l) -> BOOL
            {
                auto &m = *reinterpret_cast<Match *>(l);
                DWORD pid = 0;
                GetWindowThreadProcessId(h, &pid);
                wchar_t c[128];
                GetClassNameW(h, c, 128);
                // The panel and the strip; the strip has its own class because
                // it lives on its own thread (see src/striphost.h).
                if (pid == m.pid && (wcscmp(c, L"NativePerfMonitor.Surface.1.6") == 0 ||
                                     wcscmp(c, L"NativePerfMonitor.Strip.1.6") == 0))
                    m.windows.push_back(h);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&match));
        require(match.windows.size() == 2, "two surfaces from one monitoring process");
        HWND panel = nullptr, strip = nullptr;
        for (auto h : match.windows)
        {
            RECT r{};
            GetWindowRect(h, &r);
            if (r.bottom - r.top > 250)
                panel = h;
            else
                strip = h;
        }
        require(strip && IsWindowVisible(strip), "taskbar strip visible by default or safe fallback");
        for (auto h : match.windows)
        {
            RECT r;
            GetWindowRect(h, &r);
            require((GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0, "no taskbar button");
            require((GetWindowLongPtrW(h, GWL_EXSTYLE) &
                     (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) ==
                        (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE),
                    "locked styles");
        }
        require(panel, "desktop panel located");
        require(title(panel).find(L"CPU ") != std::wstring::npos &&
                    title(panel).find(L"processes") != std::wstring::npos,
                "live metrics and grouped application values exposed");
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
        for (auto h : {panel, strip})
        {
            RECT r;
            GetWindowRect(h, &r);
            require(r.left >= monitor.rcMonitor.left && r.top >= monitor.rcMonitor.top &&
                        r.right <= monitor.rcMonitor.right && r.bottom <= monitor.rcMonitor.bottom,
                    "primary monitor bounds");
        }
        RECT sr;
        GetWindowRect(panel, &sr);
        auto historyCount = [&]()
        {
            auto value = title(panel);
            auto pos = value.find(L"history samples ");
            return pos == value.npos ? -1 : std::stoi(value.substr(pos + 16));
        };
        require(historyCount() >= 2, "live per-core histories available");
        auto ready = [&]()
        {
            auto value = title(panel);
            size_t position = 0;
            unsigned found = 0;
            while ((position = value.find(L"\nCore ", position)) != std::wstring::npos)
            {
                auto end = value.find(L'\n', position + 1);
                if (value.substr(position, end == value.npos ? end : end - position).find(L'%') == value.npos)
                    return false;
                ++position;
                ++found;
            }
            return found > 0;
        };
        // Providers may need an extra collection after their initial baseline.
        for (unsigned i = 0; i < 60 && !ready(); ++i)
            pump(100);
        auto coreText = title(panel);
        size_t at = 0;
        unsigned liveCores = 0;
        while ((at = coreText.find(L"\nCore ", at)) != std::wstring::npos)
        {
            auto end = coreText.find(L'\n', at + 1);
            auto row = coreText.substr(at, end == std::wstring::npos ? end : end - at);
            if (row.find(L'%') == std::wstring::npos)
                std::wcout << L"Unavailable row: " << row << L"\n";
            require(row.find(L'%') != std::wstring::npos, "physical core has a valid live utilization");
            ++liveCores;
            ++at;
        }
        require(liveCores > 0, "individual core readings present");
        SendMessageW(control, WM_COMMAND, 114, 0);
        require(loadSettings(root / L"settings").opacity == 15, "opacity menu persists 15 percent");
        SendMessageW(control, WM_COMMAND, 115, 0);
        require(loadSettings(root / L"settings").opacity == 25, "opacity menu restores default");
        SendMessageW(control, WM_COMMAND, 117, 0);
        auto opacity = FindWindowW(L"NativePerfMonitor.Opacity.1.6", nullptr);
        require(opacity != nullptr, "tray command opens opacity slider");
        auto track = GetDlgItem(opacity, 501);
        require(track && SendMessageW(track, TBM_GETRANGEMIN, 0, 0) == 10 &&
                    SendMessageW(track, TBM_GETRANGEMAX, 0, 0) == 100,
                "slider range is 10 to 100");
        for (int value : {10, 57, 100})
        {
            SendMessageW(track, TBM_SETPOS, TRUE, value);
            SendMessageW(opacity, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(track));
            require(loadSettings(root / L"settings").opacity == value, "continuous opacity value persists");
        }
        SendMessageW(track, WM_KEYDOWN, VK_LEFT, 0);
        SendMessageW(track, WM_KEYUP, VK_LEFT, 0);
        require(loadSettings(root / L"settings").opacity == 99,
                "opacity slider supports one-percent keyboard steps");
        SendMessageW(opacity, WM_CLOSE, 0, 0);
        require(!IsWindow(opacity), "opacity window closes without stopping monitor");
        SendMessageW(control, WM_COMMAND, 115, 0);
        ShowWindow(panel, SW_MINIMIZE);
        pump(2200);
        require(!IsIconic(panel) && IsWindowVisible(panel), "locked panel recovers from shell minimization");
        ShowWindow(panel, SW_HIDE);
        pump(1200);
        require(IsWindowVisible(panel), "unexpected shell hide is recovered");
        SendMessageW(control, WM_COMMAND, 101, 0);
        pump(100);
        require(!IsWindowVisible(strip), "taskbar toggle hides strip");
        SendMessageW(control, WM_COMMAND, 101, 0);
        pump(100);
        require(IsWindowVisible(strip), "taskbar toggle restores strip");
        SendMessageW(control, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
        pump(400);
        require(IsWindowVisible(panel) && IsWindowVisible(strip),
                "shell recreation signal restores surfaces");
        require(title(panel).find(L"GiB") == std::wstring::npos &&
                    title(panel).find(L"MiB") == std::wstring::npos,
                "decimal units in live accessibility");
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"NativePerfMonitor.ClickThroughTest";
        wc.lpfnWndProc = DefWindowProcW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wc);
        target = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                                 L"Native monitor test target", WS_POPUP, sr.left - 10, sr.top - 10,
                                 sr.right - sr.left + 20, sr.bottom - sr.top + 20, nullptr, nullptr,
                                 wc.hInstance, nullptr);
        ShowWindow(target, SW_SHOWNOACTIVATE);
        SetWindowPos(target, HWND_TOPMOST, sr.left - 10, sr.top - 10, sr.right - sr.left + 20,
                     sr.bottom - sr.top + 20, SWP_NOACTIVATE);
        int before = historyCount();
        pump(2400);
        require(historyCount() > before, "core histories continue while fully covered");
        SendMessageW(control, WM_COMMAND, 100, 0);
        require(!IsWindowVisible(panel), "hide panel");
        before = historyCount();
        pump(2400);
        require(historyCount() > before, "core histories continue while hidden");
        SendMessageW(control, WM_COMMAND, 100, 0);
        require(IsWindowVisible(panel), "restore panel");
        // The locked panel refuses z-order changes it did not make itself; that
        // refusal is what keeps it at desktop level without any repair loop.
        SetWindowPos(panel, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        pump(200);
        require(!(GetWindowLongPtrW(panel, GWL_EXSTYLE) & WS_EX_TOPMOST),
                "locked panel refuses restacking by another process");
        // The strip genuinely overlaps the taskbar, so it is where cross-process
        // click-through can be observed: the pointer must reach the taskbar.
        {
            RECT st{};
            GetWindowRect(strip, &st);
            POINT inside{(st.left + st.right) / 2, (st.top + st.bottom) / 2};
            auto hit = WindowFromPoint(inside);
            require(hit != strip, "locked strip passes the pointer through to the taskbar");
            auto bar = FindWindowW(L"Shell_TrayWnd", nullptr);
            require(bar && GetWindow(strip, GW_OWNER) == bar, "strip is owned by the taskbar");
        }
        ShowWindow(target, SW_HIDE);
        pump(150);
        POINT center{(sr.left + sr.right) / 2, (sr.top + sr.bottom) / 2};
        auto underlying = WindowFromPoint(center);
        require(underlying != panel, "locked panel does not take the pointer");
        auto foreground = GetForegroundWindow();
        SendMessageW(control, WM_COMMAND, 102, 0);
        pump(250);
        require(!(GetWindowLongPtrW(panel, GWL_EXSTYLE) & WS_EX_TRANSPARENT), "unlock removes click-through");
        if (WindowFromPoint(center) != panel)
        {
            wchar_t cls[128]{};
            GetClassNameW(WindowFromPoint(center), cls, 128);
            std::wcout << L"Unlocked hit: " << cls << L" ";
        }
        require(WindowFromPoint(center) == panel, "unlocked panel accepts pointer targeting");
        require(GetForegroundWindow() == foreground, "lock toggle does not steal focus");
        require((GetWindowLongPtrW(panel, GWL_EXSTYLE) & WS_EX_LAYERED) &&
                    (GetWindowLongPtrW(panel, GWL_EXSTYLE) & WS_EX_LAYERED),
                "unlock retains alpha-composited surfaces");
        SendMessageW(control, WM_COMMAND, 102, 0);
        pump(150);
        require(WindowFromPoint(center) == underlying, "relocking returns the panel to the desktop layer");
        ShowWindow(target, SW_SHOWNOACTIVATE);
        // The panel must precede the desktop host in top-to-bottom enumeration.
        bool sawPanel = false;
        bool panelAboveDesktop = false;
        for (auto h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
        {
            if (h == panel)
                sawPanel = true;
            wchar_t c[128]{};
            GetClassNameW(h, c, 128);
            if ((!wcscmp(c, L"Progman") || !wcscmp(c, L"WorkerW")) && IsWindowVisible(h))
            {
                RECT r;
                GetWindowRect(h, &r);
                if (r.right - r.left >= monitor.rcMonitor.right - monitor.rcMonitor.left &&
                    r.bottom - r.top >= monitor.rcMonitor.bottom - monitor.rcMonitor.top && sawPanel)
                    panelAboveDesktop = true;
            }
        }
        if (!panelAboveDesktop)
        {
            auto anchor = static_cast<HWND>(GetPropW(panel, L"NativePerfMonitor.ZOrderAnchor"));
            wchar_t anchorClass[128]{};
            GetClassNameW(anchor, anchorClass, 128);
            std::wcout << L"Last anchor " << anchorClass << L" " << anchor << L" error="
                       << uintptr_t(GetPropW(panel, L"NativePerfMonitor.ZOrderError")) << L"\n";
            std::cout << "Primary bounds: " << monitor.rcMonitor.left << ',' << monitor.rcMonitor.top << ','
                      << monitor.rcMonitor.right << ',' << monitor.rcMonitor.bottom << "\n";
            for (auto h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
            {
                wchar_t c[128]{};
                GetClassNameW(h, c, 128);
                if (h == panel || h == panel || !wcscmp(c, L"Progman") || !wcscmp(c, L"WorkerW"))
                {
                    RECT r{};
                    GetWindowRect(h, &r);
                    std::wcout << c << L" visible=" << IsWindowVisible(h) << L" rect=" << r.left << L","
                               << r.top << L"," << r.right << L"," << r.bottom << L"\n";
                }
            }
        }
        if (!panelAboveDesktop)
        {
            wchar_t cls[128]{};
            GetClassNameW(GetShellWindow(), cls, 128);
            std::wcout << L"Shell window class: " << cls << L"\n";
            unsigned shown = 0;
            for (auto h = GetTopWindow(nullptr); h && shown < 30; h = GetWindow(h, GW_HWNDNEXT))
            {
                if (IsWindowVisible(h))
                {
                    GetClassNameW(h, cls, 128);
                    std::wcout << L"Visible class: " << cls << L" topmost="
                               << bool(GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) << L"\n";
                    ++shown;
                }
            }
        }
        require(panelAboveDesktop, "panel remains above the desktop background");
        SendMessageW(control, WM_COMMAND, 107, 0);
        pump(350);
        RECT compact;
        GetWindowRect(panel, &compact);
        std::cout << "Compact physical size: " << compact.right - compact.left << "x"
                  << compact.bottom - compact.top << "; DPI=" << GetDpiForWindow(panel) << "\n";
        require(compact.right - compact.left <= int(421 * GetDpiForWindow(panel) / 96.f),
                "compact size command");
        SendMessageW(control, WM_COMMAND, 109, 0);
        pump(1300);
        auto paused = title(panel);
        require(paused.starts_with(L"Paused."), "pause announced to accessibility");
        pump(2200);
        require(title(panel) == paused, "pause freezes readings");
        SendMessageW(control, WM_COMMAND, 109, 0);
        pump(1300);
        require(!title(panel).starts_with(L"Paused."), "resume clears paused state");
        SetWindowPos(target, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        pump(1400);
        bool sawOrdinary = false, behindOrdinary = false;
        for (auto h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
        {
            if (h == target)
                sawOrdinary = true;
            if (h == panel)
            {
                behindOrdinary = sawOrdinary;
                break;
            }
        }
        require(behindOrdinary, "locked panel stays behind a normal app window");
        auto duplicate = launch(exe, root / L"settings");
        require(WaitForSingleObject(duplicate.hProcess, 5000) == WAIT_OBJECT_0, "duplicate launch exits");
        CloseHandle(duplicate.hProcess);
        require(IsWindowVisible(panel), "duplicate launch restores instead of toggling panel off");
        DestroyWindow(target);
        target = nullptr;
        SendMessageW(control, RegisterWindowMessageW(L"NativePerfMonitor.Stop.6D845648.v1.6"), 0, 0);
        require(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0, "clean shutdown");
        DWORD code = 99;
        GetExitCodeProcess(process.hProcess, &code);
        require(code == 0, "successful application exit");
        CloseHandle(process.hProcess);
        process.hProcess = nullptr;
        auto uninstall = exe.parent_path() / L"Uninstall.exe";
        std::wstring uninstallCommand = L"\"" + uninstall.wstring() + L"\"";
        STARTUPINFOW uninstallStartup{sizeof(uninstallStartup)};
        PROCESS_INFORMATION uninstallProcess{};
        require(CreateProcessW(uninstall.c_str(), uninstallCommand.data(), nullptr, nullptr, FALSE, 0,
                               nullptr, exe.parent_path().c_str(), &uninstallStartup, &uninstallProcess),
                "launch native uninstall confirmation");
        CloseHandle(uninstallProcess.hThread);
        struct DialogMatch
        {
            DWORD pid;
            HWND dialog;
        } dialogMatch{uninstallProcess.dwProcessId, nullptr};
        for (int i = 0; i < 150 && !dialogMatch.dialog; ++i)
        {
            pump(20);
            EnumWindows(
                [](HWND h, LPARAM l) -> BOOL
                {
                    auto &m = *reinterpret_cast<DialogMatch *>(l);
                    DWORD pid = 0;
                    GetWindowThreadProcessId(h, &pid);
                    wchar_t cls[64]{};
                    GetClassNameW(h, cls, 64);
                    if (pid == m.pid && !wcscmp(cls, L"#32770"))
                    {
                        m.dialog = h;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&dialogMatch));
        }
        if (dialogMatch.dialog)
            SendMessageW(dialogMatch.dialog, WM_COMMAND, IDNO, 0);
        else
            TerminateProcess(uninstallProcess.hProcess, 3);
        auto cancelled = WaitForSingleObject(uninstallProcess.hProcess, 5000) == WAIT_OBJECT_0;
        DWORD cancelCode = 99;
        GetExitCodeProcess(uninstallProcess.hProcess, &cancelCode);
        CloseHandle(uninstallProcess.hProcess);
        require(dialogMatch.dialog != nullptr, "native uninstaller shows clear confirmation dialog");
        require(cancelled && cancelCode == 0 && std::filesystem::exists(exe) &&
                    std::filesystem::exists(uninstall),
                "cancel preserves both executables");
        std::cout << checks << " native window assertions passed\n";
        CoUninitialize();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        if (target)
            DestroyWindow(target);
        if (process.hProcess)
        {
            auto control = FindWindowW(L"NativePerfMonitor.Controller.1.6", nullptr);
            if (control)
                PostMessageW(control, RegisterWindowMessageW(L"NativePerfMonitor.Stop.6D845648.v1.6"), 0, 0);
            if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0)
                TerminateProcess(process.hProcess, 4);
            CloseHandle(process.hProcess);
        }
        CoUninitialize();
        return 1;
    }
}
