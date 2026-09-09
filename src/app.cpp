#include "collector.h"
#include "render.h"
#include "settings.h"
#include "taskbar.h"
#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <fstream>
#include <iomanip>
#include <psapi.h>
#include <shellapi.h>
#include <sstream>
#include <uiautomation.h>
#include <windowsx.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>

using namespace perf;
using Microsoft::WRL::ComPtr;
namespace
{
constexpr wchar_t controlClass[] = L"NativePerfMonitor.Controller.1";
constexpr wchar_t surfaceClass[] = L"NativePerfMonitor.Surface.1";
constexpr UINT sampleMessage = WM_APP + 1, themeMessage = WM_APP + 2, geometryMessage = WM_APP + 3,
               restoreMessage = WM_APP + 4, taskbarLayoutMessage = WM_APP + 5;
enum Command : UINT
{
    ShowPanel = 100,
    ShowStrip,
    LockSurfaces,
    MovePanel,
    MoveStrip,
    ResetPositions,
    StandardSize,
    CompactSize,
    Startup,
    Pause,
    About,
    Uninstall,
    Exit,
    InsideTaskbar,
    AdapterBase = 200
};
struct Options
{
    std::filesystem::path data, report, preview;
    unsigned benchmark = 0, warmup = 120;
    bool demo = false, exit = false, prepare = false, isolated = false;
    int theme = -1;
};
Options parseOptions()
{
    Options o;
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i)
    {
        std::wstring a = args[i];
        auto next = [&]() { return i + 1 < count ? std::wstring(args[++i]) : std::wstring(); };
        if (a == L"--data-dir")
        {
            o.data = next();
            o.isolated = true;
        }
        else if (a == L"--report")
            o.report = next();
        else if (a == L"--benchmark")
            o.benchmark = std::clamp(_wtoi(next().c_str()), 1, 86400);
        else if (a == L"--warmup")
            o.warmup = std::clamp(_wtoi(next().c_str()), 0, 3600);
        else if (a == L"--render-preview")
            o.preview = next();
        else if (a == L"--demo")
            o.demo = true;
        else if (a == L"--exit")
            o.exit = true;
        else if (a == L"--prepare-uninstall")
            o.prepare = true;
        else if (a == L"--theme")
        {
            auto t = next();
            o.theme = t == L"dark" ? 1 : t == L"light" ? 0 : -1;
        }
    }
    LocalFree(args);
    return o;
}
bool regLight(const wchar_t *name, bool fallback)
{
    DWORD value = 0, bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", name,
                        RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS
               ? value != 0
               : fallback;
}
bool highContrast()
{
    HIGHCONTRASTW h{sizeof(h)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(h), &h, 0) && (h.dwFlags & HCF_HIGHCONTRASTON);
}
uint64_t timeValue(FILETIME v)
{
    return uint64_t(v.dwHighDateTime) << 32 | v.dwLowDateTime;
}
uint64_t ownCpuTime()
{
    FILETIME c{}, e{}, k{}, u{};
    return GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u) ? timeValue(k) + timeValue(u) : 0;
}

// Read-only UIA value: includes metric labels, units, and every application column.
// Values are queried, not announced on each sample.
class ValueProvider final : public IRawElementProviderSimple, public IValueProvider
{
    LONG refs_ = 1;
    HWND window_;

  public:
    explicit ValueProvider(HWND w) : window_(w) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **p) override
    {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple))
            *p = static_cast<IRawElementProviderSimple *>(this);
        else if (id == __uuidof(IValueProvider))
            *p = static_cast<IValueProvider *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&refs_);
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *p) override
    {
        if (!p)
            return E_POINTER;
        *p = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown **p) override
    {
        if (!p)
            return E_POINTER;
        *p = nullptr;
        if (id == UIA_ValuePatternId)
        {
            *p = static_cast<IValueProvider *>(this);
            AddRef();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT *p) override
    {
        if (!p)
            return E_POINTER;
        VariantInit(p);
        if (id == UIA_NamePropertyId)
        {
            p->vt = VT_BSTR;
            p->bstrVal = SysAllocString(L"Performance monitor");
        }
        else if (id == UIA_ControlTypePropertyId)
        {
            p->vt = VT_I4;
            p->lVal = UIA_PaneControlTypeId;
        }
        else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
                 id == UIA_IsEnabledPropertyId)
        {
            p->vt = VT_BOOL;
            p->boolVal = VARIANT_TRUE;
        }
        else if (id == UIA_IsKeyboardFocusablePropertyId)
        {
            p->vt = VT_BOOL;
            p->boolVal = VARIANT_FALSE;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **p) override
    {
        return UiaHostProviderFromHwnd(window_, p);
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) override
    {
        return UIA_E_NOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR *p) override
    {
        if (!p)
            return E_POINTER;
        if (!IsWindow(window_))
            return UIA_E_ELEMENTNOTAVAILABLE;
        wchar_t text[4096]{};
        GetWindowTextW(window_, text, 4096);
        *p = SysAllocString(text);
        return *p ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *p) override
    {
        if (!p)
            return E_POINTER;
        *p = TRUE;
        return S_OK;
    }
};
struct Surface
{
    HWND hwnd = nullptr;
    bool strip = false, mica = false;
    float dpi = 96, scroll = 0;
    BitmapSurface bitmap;
    ComPtr<ID2D1HwndRenderTarget> target;
    ValueProvider *provider = nullptr;
};
class Application;
Application *application = nullptr;
class Application
{
  public:
    Options options;
    Settings settings;
    std::filesystem::path exe, data;
    HINSTANCE instance;
    HWND controller = nullptr;
    Surface panel, strip;
    Renderer renderer;
    Collector collector;
    TaskbarObserver taskbarObserver;
    bool stripHasRoom = false;
    Snapshot snapshot;
    std::vector<Adapter> adapters;
    NOTIFYICONDATAW tray{};
    UINT taskbarCreated = 0, stopMessage = 0;
    HWINEVENTHOOK foregroundHook = nullptr, objectHook = nullptr;
    HANDLE singleton = nullptr;
    winrt::Windows::UI::ViewManagement::UISettings uiSettings{nullptr};
    winrt::event_token colorToken{};
    bool dark = false, shellDark = false, contrast = false, paused = false, menuOpen = false,
         quitting = false, geometryPending = false, layingOut = false;
    uint64_t startMs = 0, lastBenchmarkMs = 0, lastBenchmarkCpu = 0, measurementStartMs = 0,
             measurementStartCpu = 0;
    std::ofstream benchmarkFile;
    std::vector<uint64_t> workingSets;
    double benchmarkCpuSum = 0;
    uint64_t peakPrivate = 0;
    unsigned benchmarkRows = 0;
    struct MenuEntry
    {
        std::wstring label;
        bool separator = false, checked = false, disabled = false, submenu = false;
    };
    std::vector<std::unique_ptr<MenuEntry>> menuEntries;
    HBRUSH menuBackground = nullptr;
    HFONT menuFont = nullptr;
    explicit Application(HINSTANCE h, Options o) : options(std::move(o)), instance(h)
    {
        exe = executablePath();
        data = options.data.empty() ? defaultDataDirectory() : std::filesystem::absolute(options.data);
        settings = loadSettings(data);
        strip.strip = true;
    }
    ~Application()
    {
        if (uiSettings && colorToken.value)
            uiSettings.ColorValuesChanged(colorToken);
        if (foregroundHook)
            UnhookWinEvent(foregroundHook);
        if (objectHook)
            UnhookWinEvent(objectHook);
        if (singleton)
            CloseHandle(singleton);
    }
    static LRESULT CALLBACK controllerProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        return application ? application->controlMessage(h, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    static LRESULT CALLBACK surfaceProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto s = reinterpret_cast<Surface *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            s = static_cast<Surface *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            s->hwnd = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
        }
        return application && s ? application->surfaceMessage(*s, m, w, l) : DefWindowProcW(h, m, w, l);
    }
    static void CALLBACK eventHook(HWINEVENTHOOK, DWORD event, HWND h, LONG object, LONG child, DWORD, DWORD)
    {
        if (!application || !h || object != OBJID_WINDOW || child != 0)
            return;
        if (event != EVENT_SYSTEM_FOREGROUND)
        {
            wchar_t cls[128];
            GetClassNameW(h, cls, 128);
            if (h != GetForegroundWindow() && wcscmp(cls, L"Shell_TrayWnd") &&
                wcscmp(cls, L"XamlExplorerHostIslandWindow") && wcscmp(cls, L"Windows.UI.Core.CoreWindow") &&
                wcscmp(cls, L"#32768"))
                return;
        }
        if (!application->geometryPending)
        {
            application->geometryPending = true;
            PostMessageW(application->controller, geometryMessage, 0, 0);
        }
    }
    bool initialize()
    {
        stopMessage = RegisterWindowMessageW(L"NativePerfMonitor.Stop.6D845648");
        singleton = CreateMutexW(nullptr, FALSE, L"Local\\NativePerfMonitor.6D845648");
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            auto existing = FindWindowW(controlClass, nullptr);
            if (existing)
                PostMessageW(existing, restoreMessage, 0, 0);
            return false;
        }
        if (!renderer.initialize())
        {
            MessageBoxW(nullptr, L"Direct2D or DirectWrite could not be initialized.", L"Performance monitor",
                        MB_ICONERROR);
            return false;
        }
        WNDCLASSEXW c{sizeof(c)};
        c.hInstance = instance;
        c.lpfnWndProc = controllerProc;
        c.lpszClassName = controlClass;
        RegisterClassExW(&c);
        c.lpfnWndProc = surfaceProc;
        c.lpszClassName = surfaceClass;
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&c);
        controller = CreateWindowExW(WS_EX_TOOLWINDOW, controlClass, L"Performance monitor controller",
                                     WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
        if (!controller)
            return false;
        DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
        panel.hwnd = CreateWindowExW(ex, surfaceClass, L"Performance monitor desktop", WS_POPUP, 0, 0, 420,
                                     548, nullptr, nullptr, instance, &panel);
        strip.hwnd = CreateWindowExW(ex, surfaceClass, L"Performance monitor strip", WS_POPUP, 0, 0, 344, 54,
                                     nullptr, nullptr, instance, &strip);
        if (!panel.hwnd || !strip.hwnd)
            return false;
        adapters = enumerateAdapters();
        if (std::none_of(adapters.begin(), adapters.end(),
                         [&](auto &a) { return a.luid == settings.adapter; }))
            settings.adapter = adapters.empty() ? 0 : adapters.front().luid;
        refreshTheme();
        try
        {
            uiSettings = winrt::Windows::UI::ViewManagement::UISettings();
            colorToken = uiSettings.ColorValuesChanged([hwnd = controller](auto &&, auto &&)
                                                       { PostMessageW(hwnd, themeMessage, 0, 0); });
        }
        catch (...)
        {
        }
        applyMode();
        taskbarObserver.start(controller, taskbarLayoutMessage);
        layout();
        addTray();
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, eventHook,
                                         0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        objectHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE, nullptr, eventHook, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        SetTimer(controller, 1, 1000, nullptr);
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        if (options.demo)
        {
            snapshot = demonstrationSnapshot();
            SetWindowTextW(panel.hwnd, renderer.accessibleText(snapshot, false).c_str());
            SetWindowTextW(strip.hwnd, renderer.accessibleText(snapshot, true).c_str());
        }
        else
            collector.start(settings.adapter, controller, sampleMessage);
        startMs = GetTickCount64();
        lastBenchmarkMs = startMs;
        lastBenchmarkCpu = ownCpuTime();
        if (options.benchmark && !options.report.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(options.report.parent_path(), ec);
            benchmarkFile.open(options.report);
            benchmarkFile << "elapsed_seconds,cpu_machine_percent,working_set_bytes,private_bytes,handles,"
                             "gdi_objects,user_objects,system_cpu_percent,gpu_percent,vram_percent,ram_"
                             "percent,eligible_processes\n";
        }
        persist();
        paintBoth();
        return true;
    }
    void persist()
    {
        std::wstring error;
        if (!saveSettings(data, settings, error))
            trayNotice(error);
    }
    void trayNotice(const std::wstring &text)
    {
        if (!tray.hWnd)
            return;
        tray.uFlags = NIF_INFO;
        wcsncpy_s(tray.szInfoTitle, L"Performance monitor", _TRUNCATE);
        wcsncpy_s(tray.szInfo, text.c_str(), _TRUNCATE);
        tray.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &tray);
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    }
    void addTray()
    {
        tray = {sizeof(tray)};
        tray.hWnd = controller;
        tray.uID = 1;
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray.uCallbackMessage = WM_APP + 10;
        tray.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        if (!tray.hIcon)
            tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcsncpy_s(tray.szTip, L"Performance monitor — right-click for controls", _TRUNCATE);
        Shell_NotifyIconW(NIM_ADD, &tray);
        tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray);
    }
    void refreshTheme()
    {
        bool nextDark = !regLight(L"AppsUseLightTheme", true);
        if (options.theme >= 0)
            nextDark = options.theme != 0;
        dark = nextDark;
        shellDark = options.theme >= 0 ? dark : !regLight(L"SystemUsesLightTheme", !dark);
        contrast = highContrast();
        if (panel.hwnd)
            applyBackdrop(panel);
        if (strip.hwnd)
            applyBackdrop(strip);
    }
    void applyBackdrop(Surface &s)
    {
        BOOL d = s.strip ? shellDark : dark;
        DwmSetWindowAttribute(s.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(s.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        // True alpha translucency works consistently when locked or unlocked.
        // Mica is a wallpaper-derived material and cannot supply this layered-window effect.
        DWM_SYSTEMBACKDROP_TYPE type = DWMSBT_NONE;
        DwmSetWindowAttribute(s.hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
        s.mica = false;
        MARGINS margins{};
        DwmExtendFrameIntoClientArea(s.hwnd, &margins);
    }
    void applyMode()
    {
        for (auto s : {&panel, &strip})
        {
            s->target.Reset();
            DWORD style = WS_POPUP | (!settings.locked && !s->strip ? WS_THICKFRAME : 0);
            DWORD ex = WS_EX_TOOLWINDOW | WS_EX_LAYERED |
                       (settings.locked ? (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE) : 0);
            SetWindowLongPtrW(s->hwnd, GWL_STYLE, style);
            SetWindowLongPtrW(s->hwnd, GWL_EXSTYLE, ex);
            SetWindowPos(s->hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            applyBackdrop(*s);
        }
        if (settings.locked)
            panel.scroll = strip.scroll = 0;
    }
    MONITORINFO primaryInfo()
    {
        MONITORINFO m{sizeof(m)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &m);
        return m;
    }
    HWND desktopAnchor()
    {
        // HWND_BOTTOM can put a tool window below Explorer's desktop host.
        // Use visible ordinary windows as anchors; hidden shell windows can live in
        // protected z-order bands and make SetWindowPos fail with access denied.
        auto monitor = primaryInfo().rcMonitor;
        HWND lastOrdinary = nullptr;
        bool sawPanel = false, ordinaryBelowPanel = false;
        for (auto h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
        {
            if (h == panel.hwnd)
            {
                sawPanel = true;
                continue;
            }
            if (h == strip.hwnd || h == controller || !IsWindowVisible(h) || IsIconic(h))
                continue;
            wchar_t cls[128]{};
            GetClassNameW(h, cls, 128);
            if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
                continue;
            const bool desktop = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW");
            if (!desktop)
            {
                if (!(GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST))
                {
                    DWORD cloaked = 0;
                    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
                    if (!cloaked)
                    {
                        lastOrdinary = h;
                        ordinaryBelowPanel = ordinaryBelowPanel || sawPanel;
                    }
                }
                continue;
            }
            RECT r{};
            GetWindowRect(h, &r);
            if (r.left > monitor.left || r.top > monitor.top || r.right < monitor.right ||
                r.bottom < monitor.bottom)
                continue;
            if (sawPanel && !ordinaryBelowPanel)
                return panel.hwnd;
            if (lastOrdinary)
                return lastOrdinary;
            // HWND_NOTOPMOST does nothing when the panel is already non-topmost.
            // Raise it above the desktop host when no ordinary window precedes it.
            return HWND_TOP;
        }
        return HWND_BOTTOM;
    }
    void layout()
    {
        if (layingOut || !panel.hwnd)
            return;
        layingOut = true;
        auto m = primaryInfo();
        UINT dpi = GetDpiForWindow(panel.hwnd);
        if (!dpi)
            dpi = 96;
        panel.dpi = strip.dpi = float(dpi);
        float scale = dpi / 96.f;
        auto px = [&](float n) { return int(std::lround(n * scale)); };
        Rect work{m.rcWork.left, m.rcWork.top, m.rcWork.right - m.rcWork.left,
                  m.rcWork.bottom - m.rcWork.top};
        int pw = settings.panelW, ph = settings.panelH;
        if (settings.compact)
        {
            pw = 360;
            ph = 460;
        }
        if ((work.h - px(48) < px(float(ph)) || work.w - px(48) < px(float(pw))) && !settings.compact)
        {
            pw = 360;
            ph = 460;
        }
        int x =
            settings.panelX < 0 ? work.x + work.w - px(float(pw + 24)) : work.x + px(float(settings.panelX));
        int y = settings.panelY < 0 ? work.y + px(24) : work.y + px(float(settings.panelY));
        auto p = clampRect({x, y, px(float(pw)), px(float(ph))}, work, px(300), px(260));
        auto anchor = settings.locked ? desktopAnchor() : HWND_TOP;
        // Geometry must succeed independently of shell z-order restrictions.
        SetWindowPos(panel.hwnd, nullptr, p.x, p.y, p.w, p.h, SWP_NOACTIVATE | SWP_NOZORDER);
        if (anchor != panel.hwnd)
        {
            SetLastError(0);
            bool placed = SetWindowPos(panel.hwnd, anchor, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
            SetPropW(panel.hwnd, L"NativePerfMonitor.ZOrderError", reinterpret_cast<HANDLE>(uintptr_t(placed ? 0 : GetLastError())));
            SetPropW(panel.hwnd, L"NativePerfMonitor.ZOrderAnchor", anchor);
        }
        APPBARDATA bar{sizeof(bar)};
        RECT task = m.rcWork;
        auto ok = SHAppBarMessage(ABM_GETTASKBARPOS, &bar);
        if (ok)
            task = bar.rc;
        int sw = px(344), sh = px(54),
            sx = settings.stripX < 0 ? work.x + work.w - sw - px(24) : work.x + px(float(settings.stripX)),
            sy = work.y + work.h - sh - px(6);
        if (ok)
        {
            if (bar.uEdge == ABE_BOTTOM)
                sy = task.top - sh - px(6);
            else if (bar.uEdge == ABE_TOP)
                sy = task.bottom + px(6);
            else if (bar.uEdge == ABE_LEFT)
                sx = task.right + px(6);
            else if (bar.uEdge == ABE_RIGHT)
                sx = task.left - sw - px(6);
        }
        auto r = clampRect({sx, sy, sw, sh},
                           {m.rcMonitor.left, m.rcMonitor.top, m.rcMonitor.right - m.rcMonitor.left,
                            m.rcMonitor.bottom - m.rcMonitor.top},
                           px(280), sh);
        stripHasRoom = !settings.insideTaskbar;
        if (settings.insideTaskbar)
        {
            auto observed = taskbarObserver.snapshot();
            auto bounds = observed.bounds;
            bool horizontal = bounds.w > bounds.h && bounds.x >= m.rcMonitor.left &&
                              bounds.x + bounds.w <= m.rcMonitor.right;
            int height = std::min(px(42), bounds.h - px(6));
            if (observed.reliable && horizontal && height >= px(28))
            {
                auto slot = taskbarSlot(bounds, observed.occupied, sw, height, sx, px(8));
                if (!slot) slot = taskbarSlot(bounds, observed.occupied, px(280), height, sx, px(8));
                if (slot)
                {
                    r = *slot;
                    stripHasRoom = true;
                }
            }
        }
        SetWindowPos(strip.hwnd, HWND_TOPMOST, r.x, r.y, r.w, r.h, SWP_NOACTIVATE);
        visibility();
        layingOut = false;
    }
    bool hideStrip()
    {
        if (!settings.locked)
            return false;
        auto m = primaryInfo();
        auto fg = GetForegroundWindow();
        wchar_t cls[128]{};
        GetClassNameW(fg, cls, 128);
        if (fg && fg != panel.hwnd && fg != strip.hwnd && wcscmp(cls, L"Progman") &&
            wcscmp(cls, L"WorkerW") && wcscmp(cls, L"Shell_TrayWnd"))
        {
            RECT r{};
            if (FAILED(DwmGetWindowAttribute(fg, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
                GetWindowRect(fg, &r);
            if (r.left <= m.rcMonitor.left && r.top <= m.rcMonitor.top && r.right >= m.rcMonitor.right &&
                r.bottom >= m.rcMonitor.bottom &&
                (!(GetWindowLongPtrW(fg, GWL_STYLE) & WS_CAPTION) || !IsZoomed(fg)))
                return true;
        }
        APPBARDATA bar{sizeof(bar)};
        if (SHAppBarMessage(ABM_GETSTATE, &bar) & ABS_AUTOHIDE)
        {
            auto task = FindWindowW(L"Shell_TrayWnd", nullptr);
            RECT r{}, visible{};
            if (task && GetWindowRect(task, &r))
            {
                IntersectRect(&visible, &r, &m.rcMonitor);
                if (!IsWindowVisible(task) || visible.bottom - visible.top < 8 ||
                    visible.right - visible.left < 8)
                    return true;
            }
        }
        struct Context
        {
            RECT strip;
            bool blocked = false;
        } ctx{};
        GetWindowRect(strip.hwnd, &ctx.strip);
        EnumWindows(
            [](HWND h, LPARAM value) -> BOOL
            {
                auto &c = *reinterpret_cast<Context *>(value);
                if (!IsWindowVisible(h) || IsIconic(h))
                    return TRUE;
                wchar_t name[128]{};
                GetClassNameW(h, name, 128);
                if (wcscmp(name, L"#32768") && wcscmp(name, L"XamlExplorerHostIslandWindow") &&
                    wcscmp(name, L"Windows.UI.Core.CoreWindow") && wcscmp(name, L"ControlCenterWindow"))
                    return TRUE;
                DWORD cloaked = 0;
                DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
                if (cloaked)
                    return TRUE;
                RECT r{}, intersection{};
                GetWindowRect(h, &r);
                if (IntersectRect(&intersection, &r, &c.strip))
                {
                    c.blocked = true;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ctx));
        return ctx.blocked;
    }
    void visibility()
    {
        show(panel, settings.panel);
        show(strip, settings.strip && stripHasRoom && !hideStrip());
    }
    void show(Surface &s, bool visible)
    {
        if (bool(IsWindowVisible(s.hwnd)) != visible)
        {
            ShowWindow(s.hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            if (visible)
                paint(s);
        }
    }
    bool occluded(Surface &s)
    {
        if (!IsWindowVisible(s.hwnd))
            return true;
        if (!settings.locked || s.strip)
            return false;
        RECT own{};
        GetWindowRect(s.hwnd, &own);
        auto region = CreateRectRgnIndirect(&own);
        for (auto h = GetWindow(s.hwnd, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV))
        {
            if (!IsWindowVisible(h) || IsIconic(h) || h == controller)
                continue;
            auto ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
            if (ex & WS_EX_LAYERED)
                continue;
            DWORD cloaked = 0;
            DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked)
                continue;
            RECT r{};
            GetWindowRect(h, &r);
            auto above = CreateRectRgn(r.left + 10, r.top + 10, r.right - 10, r.bottom - 10);
            auto remaining = CombineRgn(region, region, above, RGN_DIFF);
            DeleteObject(above);
            if (remaining == NULLREGION)
            {
                DeleteObject(region);
                return true;
            }
        }
        DeleteObject(region);
        return false;
    }
    void paint(Surface &s)
    {
        if (!IsWindowVisible(s.hwnd) || !renderer.factory())
            return;
        RECT rc{};
        GetClientRect(s.hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w <= 0 || h <= 0)
            return;
        s.dpi = float(GetDpiForWindow(s.hwnd));
        if (s.dpi < 48)
            s.dpi = 96;
        auto p = palette(s.strip ? shellDark : dark, contrast);
        if (!s.bitmap.resize(w, h))
            return;
        auto hr = renderer.drawBitmap(s.bitmap, s.dpi, snapshot, p, s.strip, settings.locked, s.scroll);
        if (FAILED(hr))
            return;
        POINT src{};
        SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(s.hwnd, nullptr, nullptr, &size, s.bitmap.dc, &src, 0, &blend, ULW_ALPHA);
    }
    void paintBoth()
    {
        paint(panel);
        paint(strip);
    }
    void update()
    {
        if (!options.demo)
            snapshot = collector.snapshot();
        SetWindowTextW(panel.hwnd, renderer.accessibleText(snapshot, false).c_str());
        SetWindowTextW(strip.hwnd, renderer.accessibleText(snapshot, true).c_str());
        if (!snapshot.paused)
        {
            if (!occluded(panel))
                paint(panel);
            if (!occluded(strip))
                paint(strip);
        }
        std::wstring tooltip = L"CPU " + formatPercent(snapshot.current[0]) + L" | GPU " +
                               formatPercent(snapshot.current[1]) + L"\nVRAM " +
                               formatBytes(snapshot.vramUsed) + L" | RAM " + formatBytes(snapshot.ramUsed);
        wcsncpy_s(tray.szTip, tooltip.c_str(), _TRUNCATE);
        tray.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &tray);
        tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    }
    std::wstring diagnostics()
    {
        auto text = L"Native Performance Monitor 1.1.0\n\n" + renderer.accessibleText(snapshot, false) +
                    L"\n\nAdapter: " + snapshot.gpuName +
                    L"\nCollection: 1 second; application ranking: 2 seconds.\n";
        text += L"CPU is busy time; application RAM is private resident memory.\nGPU and VRAM refer to the "
                L"selected adapter.\nProtected processes are omitted; unavailable values are dashes.\n\n";
        text += snapshot.status.empty() ? L"Counter status: ready.\n" : snapshot.status + L"\n";
        text += L"\nSurface: " + std::wstring(contrast ? L"high contrast / system colors"
                              : settings.locked ? L"translucent / locked / click-through"
                                                : L"translucent / unlocked");
        auto taskbar = taskbarObserver.snapshot();
        text += L"\nTaskbar strip: " + std::wstring(!settings.insideTaskbar ? L"above taskbar"
                    : stripHasRoom ? L"inside taskbar, between controls"
                                   : L"hidden: waiting for a free taskbar section");
        text += L"\nTaskbar control rectangles: " + std::to_wstring(taskbar.occupied.size());
        text += L"\nSettings: " + data.wstring() + L"\nAutostart: " +
                (startupEnabled(exe) ? std::wstring(L"enabled for this executable") : std::wstring(L"off"));
        return text;
    }
    void styleMenu(HMENU menu)
    {
        MENUINFO info{sizeof(info)};
        info.fMask = MIM_BACKGROUND;
        info.hbrBack = menuBackground;
        SetMenuInfo(menu, &info);
        for (int i = 0; i < GetMenuItemCount(menu); ++i)
        {
            wchar_t label[512]{};
            MENUITEMINFOW item{sizeof(item)};
            item.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_SUBMENU;
            item.dwTypeData = label;
            item.cch = 512;
            if (!GetMenuItemInfoW(menu, i, TRUE, &item))
                continue;
            auto entry = std::make_unique<MenuEntry>();
            entry->label = label;
            entry->separator = (item.fType & MFT_SEPARATOR) != 0;
            entry->checked = (item.fState & MFS_CHECKED) != 0;
            entry->disabled = (item.fState & MFS_DISABLED) != 0;
            entry->submenu = item.hSubMenu != nullptr;
            MENUITEMINFOW update{sizeof(update)};
            update.fMask = MIIM_FTYPE | MIIM_DATA;
            update.fType = item.fType | MFT_OWNERDRAW;
            update.dwItemData = reinterpret_cast<ULONG_PTR>(entry.get());
            SetMenuItemInfoW(menu, i, TRUE, &update);
            menuEntries.push_back(std::move(entry));
            if (item.hSubMenu)
                styleMenu(item.hSubMenu);
        }
    }
    LRESULT measureMenu(MEASUREITEMSTRUCT *m)
    {
        if (m->CtlType != ODT_MENU || !m->itemData)
            return FALSE;
        auto &item = *reinterpret_cast<MenuEntry *>(m->itemData);
        float scale = strip.dpi / 96.f;
        HDC dc = GetDC(controller);
        auto old = SelectObject(dc, menuFont);
        SIZE size{};
        GetTextExtentPoint32W(dc, item.label.c_str(), int(item.label.size()), &size);
        SelectObject(dc, old);
        ReleaseDC(controller, dc);
        m->itemWidth = UINT(size.cx + 64 * scale);
        m->itemHeight = UINT((item.separator ? 8 : 28) * scale);
        return TRUE;
    }
    LRESULT drawMenu(DRAWITEMSTRUCT *d)
    {
        if (d->CtlType != ODT_MENU || !d->itemData)
            return FALSE;
        auto &item = *reinterpret_cast<MenuEntry *>(d->itemData);
        auto p = palette(shellDark, contrast);
        auto asRgb = [](D2D1_COLOR_F c) { return RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255)); };
        bool selected = (d->itemState & ODS_SELECTED) != 0;
        auto bg = selected ? (contrast    ? GetSysColor(COLOR_HIGHLIGHT)
                              : shellDark ? RGB(55, 65, 78)
                                          : RGB(228, 235, 244))
                           : asRgb(p.surface);
        auto fg =
            selected && contrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : asRgb(item.disabled ? p.muted : p.text);
        auto brush = CreateSolidBrush(bg);
        FillRect(d->hDC, &d->rcItem, brush);
        DeleteObject(brush);
        float scale = strip.dpi / 96.f;
        auto pen = CreatePen(PS_SOLID, 1, asRgb(p.border));
        auto oldPen = SelectObject(d->hDC, pen);
        int mid = (d->rcItem.top + d->rcItem.bottom) / 2;
        if (item.separator)
        {
            MoveToEx(d->hDC, d->rcItem.left + int(12 * scale), mid, nullptr);
            LineTo(d->hDC, d->rcItem.right - int(12 * scale), mid);
        }
        else
        {
            SelectObject(d->hDC, oldPen);
            DeleteObject(pen);
            pen = CreatePen(PS_SOLID, std::max(1, int(scale)), fg);
            oldPen = SelectObject(d->hDC, pen);
            auto oldFont = SelectObject(d->hDC, menuFont);
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, fg);
            RECT r = d->rcItem;
            r.left += int(35 * scale);
            r.right -= int(25 * scale);
            DrawTextW(d->hDC, item.label.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            if (item.checked)
            {
                int x = d->rcItem.left + int(13 * scale);
                MoveToEx(d->hDC, x, mid, nullptr);
                LineTo(d->hDC, x + int(4 * scale), mid + int(4 * scale));
                LineTo(d->hDC, x + int(11 * scale), mid - int(5 * scale));
            }
            if (item.submenu)
            {
                int x = d->rcItem.right - int(14 * scale);
                MoveToEx(d->hDC, x - int(3 * scale), mid - int(4 * scale), nullptr);
                LineTo(d->hDC, x + int(1 * scale), mid);
                LineTo(d->hDC, x - int(3 * scale), mid + int(4 * scale));
            }
            SelectObject(d->hDC, oldFont);
        }
        SelectObject(d->hDC, oldPen);
        DeleteObject(pen);
        return TRUE;
    }
    void menu()
    {
        if (menuOpen)
            return;
        menuOpen = true;
        auto menu = CreatePopupMenu();
        auto add = [&](UINT id, const wchar_t *name, bool checked = false, bool disabled = false)
        { AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0) | (disabled ? MF_GRAYED : 0), id, name); };
        add(ShowPanel, L"Desktop panel", settings.panel);
        add(ShowStrip, L"Taskbar strip", settings.strip);
        add(InsideTaskbar, L"Inside taskbar", settings.insideTaskbar);
        add(LockSurfaces, L"Lock surfaces", settings.locked);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(MovePanel, L"Move / resize desktop panel…");
        add(MoveStrip, L"Move taskbar strip…");
        add(ResetPositions, L"Reset positions");
        auto gpu = CreatePopupMenu();
        for (size_t i = 0; i < adapters.size(); ++i)
            AppendMenuW(gpu, MF_STRING | (adapters[i].luid == settings.adapter ? MF_CHECKED : 0),
                        AdapterBase + UINT(i), adapters[i].name.c_str());
        if (adapters.empty())
            AppendMenuW(gpu, MF_STRING | MF_GRAYED, 0, L"No compatible GPU");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gpu), L"GPU");
        auto sizes = CreatePopupMenu();
        AppendMenuW(sizes, MF_STRING | (!settings.compact ? MF_CHECKED : 0), StandardSize, L"Default");
        AppendMenuW(sizes, MF_STRING | (settings.compact ? MF_CHECKED : 0), CompactSize, L"Compact");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sizes), L"Size");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(Startup, L"Start with Windows", startupEnabled(exe), options.isolated);
        add(Pause, L"Pause monitoring", paused);
        add(About, L"About / diagnostics…");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(Uninstall, L"Uninstall…");
        add(Exit, L"Exit");
        auto p = palette(shellDark, contrast);
        menuBackground =
            CreateSolidBrush(RGB(BYTE(p.surface.r * 255), BYTE(p.surface.g * 255), BYTE(p.surface.b * 255)));
        menuFont = CreateFontW(-int(12 * strip.dpi / 96.f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
        styleMenu(menu);
        POINT point{};
        GetCursorPos(&point);
        SetForegroundWindow(controller);
        auto command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x,
                                        point.y, controller, nullptr);
        DestroyMenu(menu);
        menuEntries.clear();
        DeleteObject(menuBackground);
        DeleteObject(menuFont);
        menuBackground = nullptr;
        menuFont = nullptr;
        PostMessageW(controller, WM_NULL, 0, 0);
        menuOpen = false;
        if (command)
            onCommand(command);
    }
    void onCommand(UINT command)
    {
        if (command >= AdapterBase && command < AdapterBase + adapters.size())
        {
            settings.adapter = adapters[command - AdapterBase].luid;
            collector.selectAdapter(settings.adapter);
            snapshot.current[1] = snapshot.current[2] = missing;
            snapshot.history.clearGpu();
            snapshot.apps.clear();
            persist();
            paintBoth();
            return;
        }
        switch (command)
        {
        case ShowPanel:
            settings.panel = !settings.panel;
            break;
        case ShowStrip:
            settings.strip = !settings.strip;
            break;
        case InsideTaskbar:
            settings.insideTaskbar = !settings.insideTaskbar;
            taskbarObserver.request();
            break;
        case LockSurfaces:
            settings.locked = !settings.locked;
            applyMode();
            break;
        case MovePanel:
        case MoveStrip:
            settings.locked = false;
            if (command == MovePanel)
                settings.panel = true;
            else
                settings.strip = true;
            applyMode();
            layout();
            ShowWindow(command == MovePanel ? panel.hwnd : strip.hwnd, SW_SHOW);
            SetForegroundWindow(command == MovePanel ? panel.hwnd : strip.hwnd);
            break;
        case ResetPositions:
            settings.panelX = settings.panelY = settings.stripX = -1;
            settings.panelW = 420;
            settings.panelH = 548;
            panel.scroll = 0;
            break;
        case StandardSize:
            settings.compact = false;
            settings.panelW = 420;
            settings.panelH = 548;
            break;
        case CompactSize:
            settings.compact = true;
            settings.panelW = 360;
            settings.panelH = 460;
            break;
        case Startup:
            if (!options.isolated)
            {
                std::wstring error;
                if (!setStartup(exe, !startupEnabled(exe), error))
                    MessageBoxW(controller, error.c_str(), L"Autostart", MB_ICONERROR);
            }
            break;
        case Pause:
            paused = !paused;
            collector.pause(paused);
            snapshot.paused = paused;
            paintBoth();
            break;
        case About:
            MessageBoxW(controller, diagnostics().c_str(), L"Performance monitor — diagnostics", MB_OK);
            return;
        case Uninstall:
        {
            auto script = exe.parent_path() / L"Uninstall.ps1";
            if (!std::filesystem::exists(script))
            {
                MessageBoxW(
                    controller,
                    L"Uninstall.ps1 is missing. Extract the complete portable ZIP before uninstalling.",
                    L"Performance monitor", MB_ICONERROR);
                return;
            }
            if (MessageBoxW(controller,
                            L"Remove this portable application's files, its settings, and its optional "
                            L"startup entry? Unrelated files will be kept.",
                            L"Uninstall Performance monitor",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                return;
            auto args = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() +
                        L"\" -Yes -DataDirectory \"" + data.wstring() + L"\"" +
                        (options.isolated ? L" -Isolated" : L"");
            SHELLEXECUTEINFOW info{sizeof(info)};
            info.fMask = SEE_MASK_NOCLOSEPROCESS;
            info.lpFile = L"powershell.exe";
            info.lpParameters = args.c_str();
            info.nShow = SW_HIDE;
            if (!ShellExecuteExW(&info))
            {
                MessageBoxW(controller, L"Could not start the uninstaller.", L"Performance monitor",
                            MB_ICONERROR);
                return;
            }
            if (info.hProcess)
                CloseHandle(info.hProcess);
            PostMessageW(controller, WM_CLOSE, 0, 0);
            return;
        }
        case Exit:
            PostMessageW(controller, WM_CLOSE, 0, 0);
            return;
        default:
            return;
        }
        layout();
        persist();
        paintBoth();
    }
    void benchmark()
    {
        if (!options.benchmark)
            return;
        auto now = GetTickCount64(), cpu = ownCpuTime();
        double elapsed = double(now - startMs) / 1000;
        if (elapsed < double(options.warmup))
        {
            lastBenchmarkMs = now;
            lastBenchmarkCpu = cpu;
            return;
        }
        if (!measurementStartMs)
        {
            measurementStartMs = now;
            measurementStartCpu = cpu;
            lastBenchmarkMs = now;
            lastBenchmarkCpu = cpu;
            return;
        }
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory));
        DWORD handles = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handles);
        double usage = cpuPercent(lastBenchmarkCpu, cpu, double(now - lastBenchmarkMs) / 1000,
                                  GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        workingSets.push_back(memory.WorkingSetSize);
        peakPrivate = std::max(peakPrivate, uint64_t(memory.PrivateUsage));
        ++benchmarkRows;
        benchmarkCpuSum += valid(usage) ? usage : 0;
        if (benchmarkFile)
        {
            benchmarkFile << std::fixed << std::setprecision(6) << double(now - measurementStartMs) / 1000
                          << ',' << usage << ',' << memory.WorkingSetSize << ',' << memory.PrivateUsage << ','
                          << handles << ',' << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << ','
                          << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
            for (auto n : snapshot.current)
                benchmarkFile << ',' << n;
            benchmarkFile << ',' << snapshot.processes << '\n';
        }
        lastBenchmarkMs = now;
        lastBenchmarkCpu = cpu;
        if (now - measurementStartMs >= uint64_t(options.benchmark) * 1000)
        {
            auto total = cpuPercent(measurementStartCpu, cpu, double(now - measurementStartMs) / 1000,
                                    GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
            std::sort(workingSets.begin(), workingSets.end());
            if (!options.report.empty())
            {
                auto summary = options.report;
                summary.replace_extension(L"summary.txt");
                std::ofstream f(summary);
                f << "NativePerfMonitor 1.1.0\nMeasured seconds: " << double(now - measurementStartMs) / 1000
                  << "\nWarm-up seconds: " << options.warmup << "\nSamples: " << benchmarkRows
                  << "\nLogical processors: " << GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)
                  << "\nAverage machine CPU percent: " << total << "\nSingle-core equivalent percent: "
                  << total * GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)
                  << "\nMedian working set bytes: " << workingSets[workingSets.size() / 2]
                  << "\n95th percentile working set bytes: "
                  << workingSets[std::min(workingSets.size() - 1, workingSets.size() * 95 / 100)]
                  << "\nPeak sampled working set bytes: " << workingSets.back()
                  << "\nPeak private bytes: " << peakPrivate << "\nDPI: " << panel.dpi
                  << "\nDesktop panel visible: " << settings.panel
                  << "\nTaskbar strip visible: " << settings.strip << "\nLocked: " << settings.locked << "\n";
            }
            benchmarkFile.flush();
            PostMessageW(controller, WM_CLOSE, 0, 0);
        }
    }
    LRESULT controlMessage(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        if (m == stopMessage && stopMessage)
        {
            PostMessageW(h, WM_CLOSE, 0, 0);
            return 0;
        }
        if (m == taskbarCreated && taskbarCreated)
        {
            addTray();
            layout();
            paintBoth();
            return 0;
        }
        switch (m)
        {
        case restoreMessage:
            settings.panel = true;
            layout();
            persist();
            paintBoth();
            return 0;
        case taskbarLayoutMessage:
            layout();
            paint(strip);
            return 0;
        case sampleMessage:
            update();
            return 0;
        case themeMessage:
        case WM_THEMECHANGED:
            refreshTheme();
            paintBoth();
            return 0;
        case WM_SETTINGCHANGE:
            refreshTheme();
            layout();
            paintBoth();
            return 0;
        case WM_DISPLAYCHANGE:
            renderer.discard();
            collector.reset();
            layout();
            paintBoth();
            return 0;
        case WM_POWERBROADCAST:
            if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND)
                collector.reset();
            return TRUE;
        case geometryMessage:
            taskbarObserver.request();
            SetTimer(h, 2, 100, nullptr);
            return 0;
        case WM_TIMER:
            if (w == 2)
            {
                KillTimer(h, 2);
                geometryPending = false;
                layout();
                if (!occluded(panel))
                    paint(panel);
            }
            else if (w == 1)
            {
                visibility();
                if (snapshot.updatedMs && GetTickCount64() > snapshot.updatedMs + 4000 && !snapshot.paused)
                    paintBoth();
                benchmark();
            }
            return 0;
        case WM_APP + 10:
            if (LOWORD(l) == WM_CONTEXTMENU || LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT)
                menu();
            return 0;
        case WM_COMMAND:
            onCommand(LOWORD(w));
            return 0;
        case WM_MEASUREITEM:
            return measureMenu(reinterpret_cast<MEASUREITEMSTRUCT *>(l));
        case WM_DRAWITEM:
            return drawMenu(reinterpret_cast<DRAWITEMSTRUCT *>(l));
        case WM_CLOSE:
            if (!quitting)
            {
                quitting = true;
                Shell_NotifyIconW(NIM_DELETE, &tray);
                collector.stop();
                taskbarObserver.stop();
                DestroyWindow(panel.hwnd);
                DestroyWindow(strip.hwnd);
                DestroyWindow(h);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    LRESULT surfaceMessage(Surface &s, UINT m, WPARAM w, LPARAM l)
    {
        switch (m)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(s.hwnd, &ps);
            paint(s);
            EndPaint(s.hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCCALCSIZE:
            if (w)
                return 0;
            break;
        case WM_NCHITTEST:
        {
            if (settings.locked)
                return HTTRANSPARENT;
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(s.hwnd, &point);
            RECT r{};
            GetClientRect(s.hwnd, &r);
            int edge = int(6 * s.dpi / 96);
            if (!s.strip)
            {
                bool left = point.x < edge, right = point.x >= r.right - edge, top = point.y < edge,
                     bottom = point.y >= r.bottom - edge;
                if (top && left)
                    return HTTOPLEFT;
                if (top && right)
                    return HTTOPRIGHT;
                if (bottom && left)
                    return HTBOTTOMLEFT;
                if (bottom && right)
                    return HTBOTTOMRIGHT;
                if (left)
                    return HTLEFT;
                if (right)
                    return HTRIGHT;
                if (top)
                    return HTTOP;
                if (bottom)
                    return HTBOTTOM;
            }
            return point.y < int(43 * s.dpi / 96) || s.strip ? HTCAPTION : HTCLIENT;
        }
        case WM_MOUSEACTIVATE:
            if (settings.locked)
                return MA_NOACTIVATE;
            break;
        case WM_CONTEXTMENU:
            menu();
            return 0;
        case WM_GETMINMAXINFO:
        {
            auto p = reinterpret_cast<MINMAXINFO *>(l);
            auto area = primaryInfo().rcWork;
            auto scale = s.dpi / 96;
            p->ptMinTrackSize = {std::min(LONG(300 * scale), area.right - area.left),
                                 std::min(LONG(260 * scale), area.bottom - area.top)};
            p->ptMaxTrackSize = {area.right - area.left, area.bottom - area.top};
            return 0;
        }
        case WM_MOVING:
        case WM_SIZING:
        {
            auto r = reinterpret_cast<RECT *>(l);
            auto info = primaryInfo();
            auto a = s.strip ? info.rcMonitor : info.rcWork;
            auto c = clampRect({r->left, r->top, r->right - r->left, r->bottom - r->top},
                               {a.left, a.top, a.right - a.left, a.bottom - a.top}, 1, 1);
            *r = {c.x, c.y, c.x + c.w, c.y + c.h};
            return TRUE;
        }
        case WM_EXITSIZEMOVE:
        {
            RECT r{};
            GetWindowRect(s.hwnd, &r);
            auto area = primaryInfo().rcWork;
            auto scale = s.dpi / 96.f;
            if (s.strip)
                settings.stripX = int((r.left - area.left) / scale);
            else
            {
                settings.panelX = int((r.left - area.left) / scale);
                settings.panelY = int((r.top - area.top) / scale);
                settings.panelW = int((r.right - r.left) / scale);
                settings.panelH = int((r.bottom - r.top) / scale);
                settings.compact = false;
            }
            layout();
            persist();
            paintBoth();
            return 0;
        }
        case WM_SIZE:
            if (!layingOut)
                paint(s);
            return 0;
        case WM_DPICHANGED:
            s.dpi = float(HIWORD(w));
            {
                auto r = reinterpret_cast<RECT *>(l);
                SetWindowPos(s.hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            layout();
            paint(s);
            return 0;
        case WM_MOUSEWHEEL:
            if (!settings.locked && !s.strip)
            {
                RECT r{};
                GetClientRect(s.hwnd, &r);
                float h = r.bottom * 96.f / s.dpi;
                float content = r.right * 96.f / s.dpi < 400 || h < 520 ? 460.f : 548.f;
                s.scroll = std::clamp(s.scroll - GET_WHEEL_DELTA_WPARAM(w) / 120.f * 36, 0.f,
                                      std::max(0.f, content - h));
                paint(s);
            }
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(l) == UiaRootObjectId)
            {
                if (!s.provider)
                    s.provider = new ValueProvider(s.hwnd);
                return UiaReturnRawElementProvider(s.hwnd, w, l, s.provider);
            }
            break;
        case WM_DESTROY:
            if (s.provider)
            {
                UiaDisconnectProvider(s.provider);
                s.provider->Release();
                s.provider = nullptr;
            }
            return 0;
        case WM_CLOSE:
            if (s.strip)
                settings.strip = false;
            else
                settings.panel = false;
            visibility();
            persist();
            return 0;
        }
        return DefWindowProcW(s.hwnd, m, w, l);
    }
};
int renderPreview(const std::filesystem::path &dir)
{
    std::filesystem::create_directories(dir);
    Renderer renderer;
    if (!renderer.initialize())
        return 2;
    auto snapshot = demonstrationSnapshot();
    for (bool dark : {false, true})
        for (bool strip : {false, true})
        {
            BitmapSurface b;
            if (!b.resize(strip ? 688 : 840, strip ? 108 : 1096))
                return 3;
            if (FAILED(renderer.drawBitmap(b, 192, snapshot, palette(dark), strip, true)))
                return 4;
            auto name = std::wstring(strip ? L"native-strip-" : L"native-panel-") +
                        (dark ? L"dark.png" : L"light.png");
            if (!b.save(dir / name))
                return 5;
        }
    return 0;
}
} // namespace
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto options = parseOptions();
    if (options.exit || options.prepare)
    {
        auto existing = FindWindowW(controlClass, nullptr);
        if (existing)
            PostMessageW(existing, RegisterWindowMessageW(L"NativePerfMonitor.Stop.6D845648"), 0, 0);
        if (options.prepare)
        {
            std::wstring error;
            return prepareUninstall(executablePath(),
                                    options.data.empty() ? defaultDataDirectory() : options.data,
                                    options.isolated, error)
                       ? 0
                       : 3;
        }
        return 0;
    }
    int result = 0;
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        if (!options.preview.empty())
            result = renderPreview(options.preview);
        else
        {
            Application app(h, std::move(options));
            application = &app;
            if (app.initialize())
            {
                MSG msg{};
                while (GetMessageW(&msg, nullptr, 0, 0) > 0)
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                result = int(msg.wParam);
            }
            application = nullptr;
        }
        winrt::uninit_apartment();
    }
    catch (const std::exception &)
    {
        MessageBoxW(nullptr,
                    L"The monitor could not finish initialization. Check that the complete portable package "
                    L"is extracted and its settings folder is writable.",
                    L"Performance monitor", MB_ICONERROR);
        result = 1;
    }
    catch (...)
    {
        result = 1;
    }
    return result;
}
