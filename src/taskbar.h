#pragma once
#include "core.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <windows.h>
namespace perf
{
struct TaskbarSnapshot
{
    Rect bounds;
    std::vector<Rect> occupied;
    bool reliable = false;
};
// UI Automation stays on its own MTA thread, away from rendering and collection.
//
// Reading the taskbar means walking Explorer's accessibility tree across a
// process boundary, which drives Explorer's own UI thread. That cost is only
// worth paying when the strip is actually being placed inside the taskbar, so
// the walk is gated on `enable` and runs on a deliberately unhurried cadence.
class TaskbarObserver
{
    HANDLE stop_ = nullptr, refresh_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    TaskbarSnapshot latest_;
    std::atomic<bool> enabled_{true};

  public:
    // Idle cadence, and the shortest gap between two walks when shell events
    // keep arriving. Both are long: the strip does not need to track the
    // taskbar closely, and a tighter loop was a measurable tax on Explorer.
    static constexpr DWORD idleIntervalMs = 8000, minimumIntervalMs = 2000;
    // Bounds differing by less than this are treated as unchanged, so a clock
    // or weather label that reflows by a pixel does not move the strip.
    static constexpr int tolerance = 6;
    TaskbarObserver();
    ~TaskbarObserver();
    void start(HWND notify, UINT message);
    void enable(bool on);
    void request();
    void stop();
    TaskbarSnapshot snapshot();
};
} // namespace perf
