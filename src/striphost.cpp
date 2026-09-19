#include "striphost.h"
#include "accessibility.h"
#include "trace.h"
#include <dwmapi.h>
#include <windowsx.h>

namespace perf
{
namespace
{
constexpr UINT frameMessage = WM_APP + 1, quitMessage = WM_APP + 2, verifyMessage = WM_APP + 3;
constexpr wchar_t mailboxClass[] = L"NativePerfMonitor.StripMailbox.2.0";

HWND taskbar()
{
    return FindWindowW(L"Shell_TrayWnd", nullptr);
}
} // namespace

StripHost::~StripHost()
{
    stop();
}

bool StripHost::start(HINSTANCE instance, StripEvents events, StripFrame first)
{
    if (thread_.joinable())
        return true;
    instance_ = instance;
    events_ = events;
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(first);
        hasPending_ = true;
    }
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready)
        return false;
    thread_ = std::thread([this, ready] { run(ready); });
    WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
    return mailbox_ != nullptr;
}

void StripHost::stop()
{
    if (!thread_.joinable())
        return;
    if (mailbox_)
        PostMessageW(mailbox_, quitMessage, 0, 0);
    thread_.join();
    mailbox_ = nullptr;
}

void StripHost::present(StripFrame frame)
{
    bool post = false;
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(frame);
        post = !hasPending_;
        hasPending_ = true;
    }
    // One wake-up per batch: if a frame is already waiting it is simply
    // replaced, so a slow strip thread never builds up a backlog.
    if (post && mailbox_)
        PostMessageW(mailbox_, frameMessage, 0, 0);
}

void StripHost::verify()
{
    if (mailbox_)
        PostMessageW(mailbox_, verifyMessage, 0, 0);
}

void StripHost::keepAboveTaskbar()
{
    auto h = window_.load();
    auto bar = taskbar();
    if (!h || !bar || !IsWindowVisible(h))
        return;
    // Walking up from the strip visits only the few windows in the topmost
    // band above it; the taskbar being among them means the strip is covered.
    bool covered = false;
    for (auto w = GetWindow(h, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV))
        if (w == bar)
        {
            covered = true;
            break;
        }
    if (!covered)
        return;
    // Directly above the taskbar, not to the top of the topmost band: the
    // strip has no business covering anything else another program keeps on top.
    auto above = GetWindow(bar, GW_HWNDPREV);
    SetWindowPos(h, above ? above : HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    trace::line(L"strip: found below the taskbar, moved back above it");
}

void StripHost::run(HANDLE ready)
{
    renderer_.initialize();
    WNDCLASSEXW c{sizeof(c)};
    c.hInstance = instance_;
    c.lpfnWndProc = mailboxProc;
    c.lpszClassName = mailboxClass;
    RegisterClassExW(&c);
    c.lpfnWndProc = windowProc;
    c.lpszClassName = windowClass;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&c);
    // Frames are delivered to a message-only window rather than to the strip
    // itself. The strip is destroyed and recreated when Explorer restarts, and
    // a message posted to a handle that has just gone away would be lost.
    mailbox_ = CreateWindowExW(0, mailboxClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance_, this);
    SetEvent(ready);
    if (!mailbox_)
        return;
    apply();
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    destroy();
    DestroyWindow(mailbox_);
}

void StripHost::create(HWND owner, const StripFrame &f)
{
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST;
    if (f.locked)
        ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    // The owner is the whole point: an owned window is kept above its owner by
    // the window manager itself, in the same operation that moves the owner.
    auto h = CreateWindowExW(ex, windowClass, L"Performance monitor strip", WS_POPUP, f.rect.x, f.rect.y,
                             f.rect.w, f.rect.h, owner, nullptr, instance_, this);
    window_ = h;
    appliedValid_ = false;
    trace::line(L"strip: created 0x%p owned by taskbar 0x%p", h, owner);
}

void StripHost::destroy()
{
    if (auto h = window_.exchange(nullptr))
    {
        DestroyWindow(h);
        trace::line(L"strip: destroyed 0x%p", h);
    }
}

void StripHost::applyStyles(bool locked)
{
    auto h = window_.load();
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST;
    if (locked)
        ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
    SetWindowPos(h, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                     SWP_FRAMECHANGED);
}

void StripHost::applyBackdrop(bool embedded, bool dark)
{
    auto h = window_.load();
    BOOL d = dark;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
    // Rounded corners and the system hairline border are what make a window
    // read as a separate card. Inside the taskbar the strip must not look like
    // one; above it, the drawn plate supplies its own outline.
    DWM_WINDOW_CORNER_PREFERENCE corner = embedded ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    COLORREF border = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(h, DWMWA_BORDER_COLOR, &border, sizeof(border));
    DWM_SYSTEMBACKDROP_TYPE type = DWMSBT_NONE;
    DwmSetWindowAttribute(h, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
}

void StripHost::apply()
{
    StripFrame f;
    {
        std::lock_guard lock(mutex_);
        if (!hasPending_)
            return;
        f = std::move(pending_);
        hasPending_ = false;
    }
    // Explorer restarting replaces the taskbar. The old owner is gone, so the
    // strip is rebuilt under the new one. Checking on every frame means this
    // does not depend on the TaskbarCreated broadcast arriving.
    auto bar = taskbar();
    auto h = window_.load();
    if (h && (!IsWindow(h) || GetWindow(h, GW_OWNER) != bar))
    {
        trace::line(L"strip: owner changed (taskbar now 0x%p), rebuilding", bar);
        destroy();
        h = nullptr;
    }
    if (!h)
    {
        create(bar, f);
        h = window_.load();
        if (!h)
            return;
    }
    const bool fresh = !appliedValid_;
    if (fresh || f.locked != current_.locked)
        applyStyles(f.locked);
    if (fresh || f.embedded != current_.embedded || f.dark != current_.dark)
        applyBackdrop(f.embedded, f.dark);
    // Position only. Ownership keeps the z-order; keepAboveTaskbar() below
    // covers the one path that bypasses it.
    if (fresh || f.rect != current_.rect)
        SetWindowPos(h, nullptr, f.rect.x, f.rect.y, f.rect.w, f.rect.h,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (fresh || f.text != current_.text)
        SetWindowTextW(h, f.text.c_str());
    const bool shown = IsWindowVisible(h) != FALSE;
    if (f.visible != shown)
    {
        ShowWindow(h, f.visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        trace::line(L"strip: %s", f.visible ? L"shown" : L"hidden");
    }
    current_ = std::move(f);
    appliedValid_ = true;
    keepAboveTaskbar();
    if (current_.visible)
        paint();
}

void StripHost::paint()
{
    auto h = window_.load();
    if (!h || !IsWindowVisible(h) || !renderer_.factory())
        return;
    RECT rc{};
    GetClientRect(h, &rc);
    if (rc.right <= 0 || rc.bottom <= 0 || !bitmap_.resize(rc.right, rc.bottom))
        return;
    float dpi = float(GetDpiForWindow(h));
    if (dpi < 48)
        dpi = 96;
    if (FAILED(renderer_.drawBitmap(bitmap_, dpi, current_.snapshot, current_.palette, true, current_.locked,
                                    0, current_.range, current_.embedded)))
        return;
    POINT src{};
    SIZE size{rc.right, rc.bottom};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(h, nullptr, nullptr, &size, bitmap_.dc, &src, 0, &blend, ULW_ALPHA);
}

LRESULT StripHost::message(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_NCHITTEST:
        // Locked, clicks fall through to the taskbar underneath; unlocked, the
        // whole strip is a drag handle.
        return current_.locked ? HTTRANSPARENT : HTCAPTION;
    case WM_MOUSEACTIVATE:
        if (current_.locked)
            return MA_NOACTIVATE;
        break;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_MINIMIZE && current_.locked)
            return 0;
        break;
    case WM_MOVING:
    {
        auto r = reinterpret_cast<RECT *>(l);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        auto c = clampRect({r->left, r->top, r->right - r->left, r->bottom - r->top},
                           {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                            mi.rcMonitor.bottom - mi.rcMonitor.top},
                           1, 1);
        *r = {c.x, c.y, c.x + c.w, c.y + c.h};
        return TRUE;
    }
    case WM_EXITSIZEMOVE:
    {
        RECT r{};
        GetWindowRect(h, &r);
        PostMessageW(events_.controller, events_.moved, 0, r.left);
        return 0;
    }
    case WM_CONTEXTMENU:
        PostMessageW(events_.controller, events_.menu, 0, 0);
        return 0;
    case WM_CLOSE:
        PostMessageW(events_.controller, events_.closed, 0, 0);
        return 0;
    case WM_DPICHANGED:
        // Geometry belongs to the UI thread, which re-lays out on its own DPI
        // notification; here only the pixels need redrawing.
        paint();
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCCALCSIZE:
        if (w)
            return 0;
        break;
    case WM_GETOBJECT:
        if (static_cast<LONG>(l) == UiaRootObjectId)
        {
            if (!provider_)
                provider_ = new ValueProvider(h);
            return UiaReturnRawElementProvider(h, w, l, provider_);
        }
        break;
    case WM_DESTROY:
        if (provider_)
        {
            UiaDisconnectProvider(provider_);
            provider_->Release();
            provider_ = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK StripHost::windowProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    auto self = reinterpret_cast<StripHost *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE)
    {
        self = static_cast<StripHost *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->message(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK StripHost::mailboxProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    auto self = reinterpret_cast<StripHost *>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (m == WM_NCCREATE)
    {
        self = static_cast<StripHost *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && m == frameMessage)
    {
        self->apply();
        return 0;
    }
    if (self && m == verifyMessage)
    {
        self->keepAboveTaskbar();
        return 0;
    }
    if (m == quitMessage)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace perf
