#pragma once
#include "core.h"
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
class TaskbarObserver
{
    HANDLE stop_ = nullptr, refresh_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    TaskbarSnapshot latest_;

  public:
    TaskbarObserver();
    ~TaskbarObserver();
    void start(HWND notify, UINT message);
    void request();
    void stop();
    TaskbarSnapshot snapshot();
};
} // namespace perf
