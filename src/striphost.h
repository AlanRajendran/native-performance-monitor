#pragma once
#include "render.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

namespace perf
{
class ValueProvider;

// Everything the strip needs to draw and place itself for one frame. Built on
// the UI thread and handed over whole, so the strip thread never reads any
// application state and the two threads share nothing but this hand-off.
struct StripFrame
{
    Rect rect{};
    bool visible = false, locked = true, embedded = false, dark = false;
    Range range = Range::Seconds;
    Palette palette{};
    Snapshot snapshot; // only history, current, paused and updatedMs are drawn
    std::wstring text; // what UI Automation reads back
};

// Messages the strip thread posts to the controller window. It never calls
// back into the application directly.
struct StripEvents
{
    HWND controller = nullptr;
    UINT moved = 0;  // the user dragged the unlocked strip; lParam = new left edge
    UINT menu = 0;   // context menu requested on the strip
    UINT closed = 0; // the strip was closed from its system menu
};

// Hosts the taskbar strip on a dedicated thread.
//
// The strip window is *owned* by Shell_TrayWnd. The window manager then keeps
// it above the taskbar as part of every z-order change the taskbar makes, so
// clicking the taskbar can no longer cover it even for a frame. Measured on
// Windows 11 25H2: a repaired topmost window was covered for 246 ms on every
// taskbar activation; an owned one for 0 ms.
//
// Ownership is not a complete guarantee. The window manager maintains it when
// the owner is repositioned normally -- which covers every taskbar click -- but
// a caller can bypass it with SWP_NOOWNERZORDER, and Explorer's Show Desktop
// does exactly that: the taskbar ends up above the strip and stays there. So
// the strip also checks, on every foreground change and every 250 ms, whether
// the taskbar is above it, and only then moves directly above the taskbar.
// The common case costs nothing and flickers never; the rare case is repaired
// within a frame or two.
//
// Ownership across processes attaches the input queue of the owning thread to
// Explorer's taskbar thread (Raymond Chen, "Is it legal to have a
// cross-process parent/child or owner/owned window relationship?"). That is
// why the strip has a thread of its own: the only thread coupled to Explorer
// is one that draws a small bitmap and never blocks, so nothing else in this
// program -- rendering the panel, menus, dialogs, counter queries -- can ever
// delay input to the taskbar.
class StripHost
{
  public:
    static constexpr wchar_t windowClass[] = L"NativePerfMonitor.Strip.1.7";

    StripHost() = default;
    StripHost(const StripHost &) = delete;
    ~StripHost();
    bool start(HINSTANCE instance, StripEvents events, StripFrame first);
    void stop();
    // Replaces the pending frame. Frames are coalesced: if the strip thread is
    // still busy, only the newest one is drawn.
    void present(StripFrame frame);
    // Asks the strip thread to confirm it is still above the taskbar, without
    // redrawing. Cheap; called on every foreground change and a short timer.
    void verify();
    HWND window() const
    {
        return window_.load();
    }

  private:
    void run(HANDLE ready);
    void apply();
    void create(HWND owner, const StripFrame &f);
    void destroy();
    void applyStyles(bool locked);
    void applyBackdrop(bool embedded, bool dark);
    void keepAboveTaskbar();
    void paint();
    LRESULT message(HWND h, UINT m, WPARAM w, LPARAM l);
    static LRESULT CALLBACK windowProc(HWND h, UINT m, WPARAM w, LPARAM l);
    static LRESULT CALLBACK mailboxProc(HWND h, UINT m, WPARAM w, LPARAM l);

    HINSTANCE instance_ = nullptr;
    StripEvents events_{};
    std::thread thread_;
    std::atomic<HWND> window_{nullptr};
    HWND mailbox_ = nullptr; // message-only window; stable while the strip is rebuilt

    std::mutex mutex_;
    StripFrame pending_;
    bool hasPending_ = false;

    // Strip-thread state only.
    StripFrame current_;
    bool appliedValid_ = false;
    Renderer renderer_;
    BitmapSurface bitmap_;
    ValueProvider *provider_ = nullptr;
};
} // namespace perf
