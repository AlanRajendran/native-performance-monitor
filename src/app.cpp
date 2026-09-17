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
constexpr wchar_t controlClass[] = L"NativePerfMonitor.Controller.1.3";
constexpr wchar_t surfaceClass[] = L"NativePerfMonitor.Surface.1.3";
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
    RangeSeconds = 130,
    RangeMinutes = 131,
    Opacity15 = 114,
    Opacity25 = 115,
    Opacity40 = 116,
    OpacitySlider = 117,
    InstallApp = 118,
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
    bool strip = false;
    float dpi = 96, scroll = 0;
    BitmapSurface bitmap;
    ValueProvider *provider = nullptr;
    // Last geometry, z-order anchor and visibility actually handed to the shell.
    // Placement compares against these and calls nothing when they still hold.
    Rect applied{};
    HWND appliedAfter = nullptr;
    int appliedVisible = -1;
    bool appliedValid = false;
    std::wstring appliedText;
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
    TaskbarObserver taskbarObserver;
    bool stripHasRoom = true, stripInside = false, stripPlacedInside = false;
    // Placement damping. Every one of these exists to stop an oscillating
    // input from reaching the screen; see docs/placement.md.
    static constexpr uint64_t restackIntervalMs = 2000; // minimum gap between z-order repairs
    static constexpr uint64_t stripSettleMs = 1500;     // minimum gap between taskbar slot moves
    static constexpr uint64_t stripRestoreMs = 700;     // quiet time before the strip comes back
    static constexpr int stripDeadband = 12;            // slot drift ignored outright, in pixels
    static constexpr UINT geometryDelayMs = 250;        // quiet time before a shell event is acted on
    uint64_t panelRestackMs = 0, stripMovedMs = 0, stripBlockedMs = 0;
    HWND opacityWindow = nullptr, opacityTrack = nullptr, opacityValue = nullptr;
    HBRUSH opacityBrush = nullptr;
    HFONT opacityFont = nullptr;
    Renderer renderer;
    Collector collector;
    Snapshot snapshot;
    std::vector<Adapter> adapters;
    NOTIFYICONDATAW tray{};
    UINT taskbarCreated = 0, stopMessage = 0;
    HWINEVENTHOOK foregroundHook = nullptr, objectHook = nullptr, minimizeHook = nullptr;
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
        if (minimizeHook)
            UnhookWinEvent(minimizeHook);
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
    // Runs on the message loop for every matching event in every process, so it
    // must stay cheap and must not itself cause more events. It records that a
    // pass is wanted and returns; the pass happens once, later, on a timer.
    static void CALLBACK eventHook(HWINEVENTHOOK, DWORD event, HWND h, LONG object, LONG child, DWORD, DWORD)
    {
        if (!application || !h || object != OBJID_WINDOW || child != 0)
            return;
        if (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_HIDE)
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
        stopMessage = RegisterWindowMessageW(L"NativePerfMonitor.Stop.6D845648.v1.3");
        singleton = CreateMutexW(nullptr, FALSE, L"Local\\NativePerfMonitor.6D845648.v1.3");
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
        panel.hwnd = CreateWindowExW(ex, surfaceClass, L"Performance monitor desktop", WS_POPUP, 0, 0, 450,
                                     880, nullptr, nullptr, instance, &panel);
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
        syncObserver();
        layout();
        addTray();
        // Only the three things that can actually invalidate placement are
        // observed. The previous range ran from EVENT_OBJECT_SHOW to
        // EVENT_OBJECT_LOCATIONCHANGE, which also delivered focus, selection,
        // state-change and location-change events from every window in every
        // process: a continuous stream during any drag, resize or animation,
        // and the main reason the surfaces stuttered on busy desktops.
        foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, eventHook,
                                         0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        objectHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, eventHook, 0, 0,
                                     WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr,
                                       eventHook, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        SetTimer(controller, 1, 1000, nullptr);
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        if (options.demo)
        {
            snapshot = demonstrationSnapshot();
            describe(panel);
            describe(strip);
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
                             "percent,eligible_processes,cores,valid_cores\n";
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
        if (opacityWindow)
        {
            if (opacityBrush)
                DeleteObject(opacityBrush);
            opacityBrush = CreateSolidBrush(dark && !contrast ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW));
            BOOL d = dark;
            DwmSetWindowAttribute(opacityWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
            RedrawWindow(opacityWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        }
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
        MARGINS margins{};
        DwmExtendFrameIntoClientArea(s.hwnd, &margins);
    }
    void applyMode()
    {
        for (auto s : {&panel, &strip})
        {
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
            panel.scroll = 0;
        // Lock state changes the window styles and the z-order band, so the
        // cached placement no longer describes these windows.
        invalidatePlacement();
    }
    MONITORINFO primaryInfo()
    {
        MONITORINFO m{sizeof(m)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &m);
        return m;
    }
    // ---------------------------------------------------------------------
    // Placement is edge triggered. Each pass computes what the surfaces should
    // look like, compares that against what was last handed to the shell, and
    // calls into the window manager only for the parts that actually differ.
    // An input that oscillates therefore stops at the comparison instead of
    // turning into visible movement.
    // ---------------------------------------------------------------------
    void applyGeometry(Surface &s, Rect want, HWND after = nullptr)
    {
        const bool fresh = !s.appliedValid;
        const bool moved = fresh || s.applied != want;
        const bool resized = fresh || s.applied.w != want.w || s.applied.h != want.h;
        const bool restacking = after && (fresh || s.appliedAfter != after);
        if (!moved && !restacking)
            return;
        UINT flags = SWP_NOACTIVATE;
        if (!moved)
            flags |= SWP_NOMOVE | SWP_NOSIZE;
        if (!restacking)
            flags |= SWP_NOZORDER;
        SetLastError(0);
        const bool placed = SetWindowPos(s.hwnd, restacking ? after : nullptr, want.x, want.y, want.w, want.h,
                                         flags) != FALSE;
        if (restacking)
        {
            SetPropW(s.hwnd, L"NativePerfMonitor.ZOrderError",
                     reinterpret_cast<HANDLE>(uintptr_t(placed ? 0 : GetLastError())));
            SetPropW(s.hwnd, L"NativePerfMonitor.ZOrderAnchor", after);
            s.appliedAfter = after;
        }
        s.applied = want;
        s.appliedValid = true;
        // A layered window keeps its old bitmap across a resize, so the new
        // extent would show stale pixels until something else repainted it.
        if (resized && !fresh)
            paint(s);
    }
    // True while an application owns the whole monitor without a caption, which
    // is what an exclusive full-screen game or a video player looks like.
    // Reordering windows underneath one can drop it out of its presentation
    // mode, so placement leaves the z-order alone while this holds.
    bool fullscreenForeground()
    {
        auto fg = GetForegroundWindow();
        if (!fg || fg == panel.hwnd || fg == strip.hwnd || fg == controller)
            return false;
        wchar_t cls[128]{};
        GetClassNameW(fg, cls, 128);
        if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd"))
            return false;
        auto m = primaryInfo();
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(fg, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
            GetWindowRect(fg, &r);
        return r.left <= m.rcMonitor.left && r.top <= m.rcMonitor.top && r.right >= m.rcMonitor.right &&
               r.bottom >= m.rcMonitor.bottom &&
               (!(GetWindowLongPtrW(fg, GWL_STYLE) & WS_CAPTION) || !IsZoomed(fg));
    }
    // Forgets what the shell was last told, so the next pass reapplies
    // everything. Called for the events that genuinely invalidate placement --
    // Explorer restarting, a display change, a DPI change, a lock change --
    // rather than re-asserting z-order on a timer, which is what made the
    // surfaces fight the shell for position.
    // Walking Explorer's accessibility tree only earns its cost while the strip
    // is actually being fitted into the taskbar.
    void syncObserver()
    {
        taskbarObserver.enable(settings.strip && settings.insideTaskbar);
    }
    void invalidatePlacement()
    {
        for (auto s : {&panel, &strip})
        {
            s->appliedValid = false;
            s->appliedAfter = nullptr;
        }
        panelRestackMs = 0;
        stripMovedMs = 0;
    }
    // Re-stacking is the most disruptive thing this program does to the rest of
    // the desktop, so it is rate limited and skipped entirely while a
    // full-screen application owns the foreground.
    void restack()
    {
        if (!settings.locked)
        {
            applyGeometry(panel, panel.applied, HWND_TOP);
            return;
        }
        const auto now = GetTickCount64();
        if (panel.appliedValid && panelRestackMs && now - panelRestackMs < restackIntervalMs)
            return;
        panelRestackMs = now;
        if (fullscreenForeground())
            return;
        auto anchor = desktopAnchor();
        if (anchor != panel.hwnd)
            applyGeometry(panel, panel.applied, anchor);
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
            if (h == controller || h == strip.hwnd || h == opacityWindow || !IsWindowVisible(h) ||
                IsIconic(h))
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
        if (!panel.hwnd)
            return;
        if (layingOut)
        {
            PostMessageW(controller, geometryMessage, 0, 0);
            return;
        }
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
            pw = 420;
            ph = 720;
        }
        if ((work.h - px(48) < px(float(ph)) || work.w - px(48) < px(float(pw))) && !settings.compact)
        {
            pw = 420;
            ph = 720;
        }
        int x =
            settings.panelX < 0 ? work.x + work.w - px(float(pw + 24)) : work.x + px(float(settings.panelX));
        int y = settings.panelY < 0 ? work.y + px(24) : work.y + px(float(settings.panelY));
        auto p = clampRect({x, y, px(float(pw)), px(float(ph))}, work, px(360), px(300));
        // Geometry is applied unconditionally of z-order, which the shell may
        // refuse; restacking is a separate, rate-limited decision below.
        applyGeometry(panel, p);
        restack();
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
        stripHasRoom = true;
        stripInside = false;
        if (settings.insideTaskbar)
        {
            auto observed = taskbarObserver.snapshot();
            auto bounds = observed.bounds;
            bool horizontal = bounds.w > bounds.h && bounds.x >= m.rcMonitor.left &&
                              bounds.x + bounds.w <= m.rcMonitor.right;
            int height = std::min(px(42), bounds.h - px(6));
            if (observed.reliable && horizontal && height >= px(28))
            {
                // Prefer the slot already in use. Re-picking the nearest gap on
                // every taskbar change makes the strip hop each time an icon,
                // a badge or the weather widget changes width.
                int preferred = stripPlacedInside && strip.appliedValid ? strip.applied.x : sx;
                auto slot = taskbarSlot(bounds, observed.occupied, sw, height, preferred, px(8));
                if (!slot)
                    slot = taskbarSlot(bounds, observed.occupied, px(280), height, preferred, px(8));
                if (slot)
                {
                    r = *slot;
                    stripInside = true;
                }
            }
        }
        r = settleStrip(r);
        applyGeometry(strip, r, HWND_TOPMOST);
        stripPlacedInside = stripInside;
        visibility();
        layingOut = false;
    }
    // Holds the strip still unless the taskbar really has no room where it is.
    // Small slot changes are ignored outright and any accepted move is rate
    // limited, so a busy taskbar cannot translate into a twitching strip.
    Rect settleStrip(Rect wanted)
    {
        if (!strip.appliedValid || stripInside != stripPlacedInside || wanted.w != strip.applied.w ||
            wanted.h != strip.applied.h || wanted.y != strip.applied.y)
            return wanted;
        const int drift = std::abs(wanted.x - strip.applied.x);
        if (!drift)
            return wanted;
        const auto now = GetTickCount64();
        if (drift < stripDeadband || now - stripMovedMs < stripSettleMs)
            return strip.applied;
        stripMovedMs = now;
        return wanted;
    }
    // Debounced wrapper around hideStrip(). Suppression takes effect at once so
    // a menu is never covered, but restoring waits for the obstruction to stay
    // gone. A blink needs a hide followed promptly by a show, and this makes
    // that second half impossible.
    bool stripSuppressed()
    {
        if (hideStrip())
        {
            stripBlockedMs = GetTickCount64();
            return true;
        }
        return stripBlockedMs && GetTickCount64() - stripBlockedMs < stripRestoreMs;
    }
    bool hideStrip()
    {
        if (!settings.locked)
            return false;
        auto m = primaryInfo();
        if (fullscreenForeground())
            return true;
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
        show(strip, settings.strip && !stripSuppressed());
    }
    void show(Surface &s, bool visible)
    {
        if (bool(IsWindowVisible(s.hwnd)) != visible || (visible && IsIconic(s.hwnd)))
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
        if (!contrast)
            p.surface.a = s.strip ? .62f : settings.opacity / 100.f;
        if (!s.bitmap.resize(w, h))
            return;
        auto hr = renderer.drawBitmap(s.bitmap, s.dpi, snapshot, p, s.strip, settings.locked, s.scroll,
                                      settings.range);
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
    void describe(Surface &s)
    {
        // Window text is what the UI Automation provider reads back. Setting it
        // broadcasts a name-change event system wide, so only write real changes.
        auto text = renderer.accessibleText(snapshot, s.strip, settings.range);
        if (text == s.appliedText)
            return;
        s.appliedText = text;
        SetWindowTextW(s.hwnd, text.c_str());
    }
    void update()
    {
        if (!options.demo)
            snapshot = collector.snapshot();
        describe(panel);
        describe(strip);
        if (!snapshot.paused)
        {
            if (!occluded(panel))
                paint(panel);
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
        auto text =
            L"Native Performance Monitor 1.4.0\n\n" +
            renderer.accessibleText(snapshot, false, settings.range) + L"\n\nAdapter: " + snapshot.gpuName +
            L"\nCollection: 1 second; application ranking: 2 seconds.\nHistory range: " +
            (settings.range == Range::Minutes ? L"60 minutes (one-minute averages)." : L"60 seconds.") +
            L"\n";
        text += L"CPU is busy time; application RAM is private resident memory.\nGPU and VRAM refer to the "
                L"selected adapter.\nProtected processes are omitted; unavailable values are dashes.\n\n";
        text += snapshot.status.empty() ? L"Counter status: ready.\n" : snapshot.status + L"\n";
        text += L"\nSurface: " + std::wstring(contrast          ? L"high contrast / system colors"
                                              : settings.locked ? L"translucent / locked / click-through"
                                                                : L"translucent / unlocked");
        text += L"\nPer-core history continues while hidden. Background opacity follows "
                L"the tray setting.";
        text += L"\nSettings: " + data.wstring() + L"\nAutostart: " +
                (startupEnabled(exe) ? std::wstring(L"enabled for this executable") : std::wstring(L"off"));
        text += L"\nTaskbar strip: " + std::wstring(!settings.strip ? L"off"
                                                    : stripInside   ? L"inside taskbar"
                                                                    : L"above taskbar (safe fallback)");
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
        float scale = panel.dpi / 96.f;
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
        float scale = panel.dpi / 96.f;
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
        add(LockSurfaces, L"Lock panel", settings.locked);
        add(ShowStrip, L"Taskbar strip", settings.strip);
        add(InsideTaskbar, L"Prefer inside taskbar", settings.insideTaskbar);
        add(OpacitySlider, L"Panel opacity…");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(MovePanel, L"Move / resize desktop panel…");
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
        auto history = CreatePopupMenu();
        const bool longRange = settings.range == Range::Minutes;
        AppendMenuW(history, MF_STRING | (!longRange ? MF_CHECKED : 0), RangeSeconds, L"60 seconds");
        AppendMenuW(history, MF_STRING | (longRange ? MF_CHECKED : 0), RangeMinutes, L"60 minutes");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(history), L"History");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(InstallApp, L"Install for this user…", false, options.isolated);
        add(Startup, L"Start with Windows", startupEnabled(exe), options.isolated);
        add(Pause, L"Pause monitoring", paused);
        add(About, L"About / diagnostics…");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        add(Uninstall, L"Uninstall…");
        add(Exit, L"Exit");
        auto p = palette(shellDark, contrast);
        menuBackground =
            CreateSolidBrush(RGB(BYTE(p.surface.r * 255), BYTE(p.surface.g * 255), BYTE(p.surface.b * 255)));
        menuFont = CreateFontW(-int(12 * panel.dpi / 96.f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
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
    static LRESULT CALLBACK opacityProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto app = reinterpret_cast<Application *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            app = static_cast<Application *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app)
            return DefWindowProcW(h, m, w, l);
        switch (m)
        {
        case WM_HSCROLL:
            if (reinterpret_cast<HWND>(l) == app->opacityTrack)
            {
                app->settings.opacity =
                    std::clamp(int(SendMessageW(app->opacityTrack, TBM_GETPOS, 0, 0)), 10, 100);
                std::wstring label = std::to_wstring(app->settings.opacity) + L"% opacity";
                if (app->settings.opacity == 100)
                    label += L" (opaque)";
                SetWindowTextW(app->opacityValue, label.c_str());
                app->paint(app->panel);
                if (LOWORD(w) != TB_THUMBTRACK)
                    app->persist();
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(w), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(w),
                         app->dark && !app->contrast ? RGB(240, 240, 240) : GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(app->opacityBrush);
        case WM_ERASEBKGND:
        {
            RECT r{};
            GetClientRect(h, &r);
            FillRect(reinterpret_cast<HDC>(w), &r, app->opacityBrush);
            return 1;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL)
            {
                DestroyWindow(h);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            app->persist();
            app->opacityWindow = app->opacityTrack = app->opacityValue = nullptr;
            if (app->opacityBrush)
                DeleteObject(app->opacityBrush);
            app->opacityBrush = nullptr;
            if (app->opacityFont)
                DeleteObject(app->opacityFont);
            app->opacityFont = nullptr;
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    void openOpacity()
    {
        if (opacityWindow)
        {
            ShowWindow(opacityWindow, SW_SHOWNORMAL);
            SetForegroundWindow(opacityWindow);
            return;
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        WNDCLASSEXW c{sizeof(c)};
        c.hInstance = instance;
        c.lpfnWndProc = opacityProc;
        c.lpszClassName = L"NativePerfMonitor.Opacity.1.3";
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&c);
        auto area = primaryInfo().rcWork;
        UINT dpi = UINT(panel.dpi);
        if (dpi < 48)
            dpi = 96;
        auto px = [&](int n) { return MulDiv(n, int(dpi), 96); };
        RECT r{0, 0, px(360), px(152)};
        DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP;
        AdjustWindowRectExForDpi(&r, style, FALSE, WS_EX_TOOLWINDOW, dpi);
        opacityBrush = CreateSolidBrush(dark && !contrast ? RGB(32, 32, 32) : GetSysColor(COLOR_WINDOW));
        opacityFont = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, 0, L"Segoe UI");
        opacityWindow = CreateWindowExW(WS_EX_TOOLWINDOW, c.lpszClassName, L"Panel opacity", style,
                                        area.right - (r.right - r.left) - px(20),
                                        area.bottom - (r.bottom - r.top) - px(20), r.right - r.left,
                                        r.bottom - r.top, controller, nullptr, instance, this);
        if (!opacityWindow)
        {
            DeleteObject(opacityBrush);
            DeleteObject(opacityFont);
            opacityBrush = nullptr;
            opacityFont = nullptr;
            return;
        }
        BOOL d = dark;
        DwmSetWindowAttribute(opacityWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
        auto child = [&](const wchar_t *cls, const wchar_t *text, DWORD flags, int x, int y, int width,
                         int height, int id)
        {
            auto h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | flags, px(x), px(y), px(width),
                                     px(height), opacityWindow, reinterpret_cast<HMENU>(INT_PTR(id)),
                                     instance, nullptr);
            SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(opacityFont), TRUE);
            return h;
        };
        opacityValue = child(L"STATIC", L"", 0, 18, 12, 320, 24, 502);
        opacityTrack = child(TRACKBAR_CLASSW, L"Panel opacity", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 12, 40,
                             334, 32, 501);
        SendMessageW(opacityTrack, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
        SendMessageW(opacityTrack, TBM_SETPOS, TRUE, settings.opacity);
        SendMessageW(opacityTrack, TBM_SETPAGESIZE, 0, 10);
        child(L"STATIC", L"10% (transparent)", 0, 18, 76, 190, 22, 503);
        child(L"STATIC", L"100% (opaque)", SS_RIGHT, 208, 76, 134, 22, 504);
        child(L"BUTTON", L"Done", BS_DEFPUSHBUTTON | WS_TABSTOP, 254, 112, 88, 28, IDOK);
        SendMessageW(opacityWindow, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(opacityTrack));
        ShowWindow(opacityWindow, SW_SHOWNORMAL);
        SetForegroundWindow(opacityWindow);
        SetFocus(opacityTrack);
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
        case Opacity15:
        case Opacity25:
        case Opacity40:
            settings.opacity = command == Opacity15 ? 15 : command == Opacity25 ? 25 : 40;
            break;
        case OpacitySlider:
            openOpacity();
            return;
        case InstallApp:
        {
            if (options.isolated)
                return;
            auto setup = exe.parent_path() / L"Setup.exe";
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(controller, L"open", setup.c_str(), nullptr,
                                                        exe.parent_path().c_str(), SW_SHOWNORMAL)) <= 32)
                MessageBoxW(controller, L"Open Setup.exe from the extracted package to install this version.",
                            L"Install", MB_ICONINFORMATION);
            return;
        }
        case ShowStrip:
            settings.strip = !settings.strip;
            syncObserver();
            break;
        case InsideTaskbar:
            settings.insideTaskbar = !settings.insideTaskbar;
            syncObserver();
            break;
        case RangeSeconds:
        case RangeMinutes:
        {
            auto next = command == RangeMinutes ? Range::Minutes : Range::Seconds;
            if (next == settings.range)
                return;
            settings.range = next;
            // Both ranges are always being recorded, so the other one is
            // already populated; only the drawing needs to be redone.
            persist();
            paintBoth();
            return;
        }
        case ShowPanel:
            settings.panel = !settings.panel;
            break;
        case LockSurfaces:
            settings.locked = !settings.locked;
            applyMode();
            break;
        case MovePanel:
            settings.locked = false;
            settings.panel = true;
            applyMode();
            layout();
            ShowWindow(panel.hwnd, SW_SHOW);
            SetForegroundWindow(panel.hwnd);
            break;
        case ResetPositions:
            settings.panelX = settings.panelY = settings.stripX = -1;
            settings.panelW = 450;
            settings.panelH = 880;
            panel.scroll = 0;
            break;
        case StandardSize:
            settings.compact = false;
            settings.panelW = 450;
            settings.panelH = 880;
            break;
        case CompactSize:
            settings.compact = true;
            settings.panelW = 420;
            settings.panelH = 720;
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
            auto uninstaller = exe.parent_path() / L"Uninstall.exe";
            if (!std::filesystem::exists(uninstaller))
            {
                MessageBoxW(controller, L"Uninstall.exe is missing. Extract the complete portable ZIP.",
                            L"Performance monitor", MB_ICONERROR);
                return;
            }
            auto result = ShellExecuteW(controller, L"open", uninstaller.c_str(), nullptr,
                                        exe.parent_path().c_str(), SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
                MessageBoxW(controller, L"Could not open Uninstall.exe.", L"Performance monitor",
                            MB_ICONERROR);
            return;
        }
        case Exit:
            PostMessageW(controller, WM_CLOSE, 0, 0);
            return;
        default:
            return;
        }
        // A command the user just issued is an explicit request, so the cached
        // placement should not damp it.
        invalidatePlacement();
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
            benchmarkFile << ',' << snapshot.processes << ',' << snapshot.cores.size() << ','
                          << std::count_if(snapshot.cores.begin(), snapshot.cores.end(),
                                           [](const auto &core) { return valid(core.current); })
                          << '\n';
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
                f << "NativePerfMonitor 1.3.0\nMeasured seconds: " << double(now - measurementStartMs) / 1000
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
            // Explorer restarted: the new taskbar sits above the strip and the
            // desktop host is a different window, so nothing cached still holds.
            addTray();
            invalidatePlacement();
            taskbarObserver.request();
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
            invalidatePlacement();
            layout();
            paintBoth();
            return 0;
        case WM_DISPLAYCHANGE:
            renderer.discard();
            collector.reset();
            invalidatePlacement();
            layout();
            paintBoth();
            return 0;
        case WM_POWERBROADCAST:
            if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND)
                collector.reset();
            return TRUE;
        case taskbarLayoutMessage:
            layout();
            paint(strip);
            return 0;
        case geometryMessage:
            taskbarObserver.request();
            SetTimer(h, 2, geometryDelayMs, nullptr);
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
                layout();
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
                if (opacityWindow)
                    DestroyWindow(opacityWindow);
                DestroyWindow(strip.hwnd);
                DestroyWindow(panel.hwnd);
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
        case WM_SYSCOMMAND:
            if ((w & 0xfff0) == SC_MINIMIZE && settings.locked)
                return 0;
            break;
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
            p->ptMinTrackSize = {std::min(LONG(360 * scale), area.right - area.left),
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
            // The user just chose a position by hand; adopt it as the new
            // baseline rather than letting the cache drag the window back.
            invalidatePlacement();
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
            // A live drag-resize delivers a stream of these, and each repaint is
            // a full software render of the whole surface. Coalesce them.
            if (!layingOut)
            {
                s.applied = {};
                s.appliedValid = false;
                paint(s);
            }
            return 0;
        case WM_DPICHANGED:
            s.dpi = float(HIWORD(w));
            {
                auto r = reinterpret_cast<RECT *>(l);
                SetWindowPos(s.hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            invalidatePlacement();
            layout();
            paint(s);
            return 0;
        case WM_MOUSEWHEEL:
            if (!settings.locked && !s.strip)
            {
                RECT r{};
                GetClientRect(s.hwnd, &r);
                float h = r.bottom * 96.f / s.dpi;
                float content = panelContentHeight(snapshot.cores);
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
    // Every combination that can look wrong on its own: both themes, both
    // surfaces, both history ranges, and the extremes of the opacity slider.
    for (bool dark : {false, true})
        for (bool strip : {false, true})
            for (auto range : {Range::Seconds, Range::Minutes})
                for (int opacity : {10, 25, 100})
                {
                    BitmapSurface b;
                    const int w = strip ? 688 : 900, h = strip ? 108 : 1760;
                    if (!b.resize(w, h))
                        return 3;
                    auto p = palette(dark);
                    p.surface.a = strip ? .62f : opacity / 100.f;
                    if (FAILED(renderer.drawBitmap(b, 192, snapshot, p, strip, true, 0, range)))
                        return 4;
                    auto name = std::wstring(strip ? L"strip-" : L"panel-") + (dark ? L"dark-" : L"light-") +
                                (range == Range::Minutes ? L"60min-" : L"60sec-") + std::to_wstring(opacity) +
                                L".png";
                    if (!b.save(dir / name))
                        return 5;
                    if (strip)
                        break; // the strip keeps its own fixed background
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
            PostMessageW(existing, RegisterWindowMessageW(L"NativePerfMonitor.Stop.6D845648.v1.3"), 0, 0);
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
                    if (app.opacityWindow && IsDialogMessageW(app.opacityWindow, &msg))
                        continue;
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
