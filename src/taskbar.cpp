#include "taskbar.h"
#include <algorithm>
#include <objbase.h>
#include <uiautomation.h>
#include <wrl/client.h>
namespace perf
{
using Microsoft::WRL::ComPtr;
static TaskbarSnapshot readTaskbar(IUIAutomation *automation)
{
    TaskbarSnapshot result;
    auto taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT bar{};
    if (!taskbar || !GetWindowRect(taskbar, &bar) || !automation)
        return result;
    result.bounds = {bar.left, bar.top, bar.right - bar.left, bar.bottom - bar.top};
    ComPtr<IUIAutomationElement> root;
    ComPtr<IUIAutomationCacheRequest> cache;
    ComPtr<IUIAutomationCondition> all;
    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(automation->ElementFromHandle(taskbar, &root)) ||
        FAILED(automation->CreateCacheRequest(&cache)) || FAILED(automation->CreateTrueCondition(&all)))
        return result;
    cache->put_TreeScope(TreeScope_Element);
    cache->put_AutomationElementMode(AutomationElementMode_None);
    cache->AddProperty(UIA_BoundingRectanglePropertyId);
    cache->AddProperty(UIA_ControlTypePropertyId);
    cache->AddProperty(UIA_IsOffscreenPropertyId);
    cache->AddProperty(UIA_IsKeyboardFocusablePropertyId);
    if (FAILED(root->FindAllBuildCache(TreeScope_Descendants, all.Get(), cache.Get(), &elements)))
        return result;
    int count = 0;
    if (FAILED(elements->get_Length(&count)) || count < 1 || count > 2048)
        return result;
    for (int i = 0; i < count; ++i)
    {
        ComPtr<IUIAutomationElement> element;
        CONTROLTYPEID type = 0;
        BOOL offscreen = TRUE, focusable = FALSE;
        RECT r{}, overlap{};
        if (FAILED(elements->GetElement(i, &element)) || FAILED(element->get_CachedControlType(&type)) ||
            FAILED(element->get_CachedIsOffscreen(&offscreen)) || offscreen ||
            FAILED(element->get_CachedBoundingRectangle(&r)))
            continue;
        element->get_CachedIsKeyboardFocusable(&focusable);
        bool control = focusable || type == UIA_ButtonControlTypeId || type == UIA_ListItemControlTypeId ||
                       type == UIA_CheckBoxControlTypeId || type == UIA_MenuItemControlTypeId ||
                       type == UIA_EditControlTypeId || type == UIA_TabItemControlTypeId;
        // Container panes span the entire taskbar; only actual control rectangles reserve space.
        if (!control || !IntersectRect(&overlap, &bar, &r) || r.right - r.left >= result.bounds.w)
            continue;
        result.occupied.push_back(
            {overlap.left, overlap.top, overlap.right - overlap.left, overlap.bottom - overlap.top});
    }
    // Reserve the complete notification area, including gaps between its controls.
    auto tray = FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT trayRect{}, intersection{};
    if (tray && GetWindowRect(tray, &trayRect) && IntersectRect(&intersection, &bar, &trayRect))
        result.occupied.push_back({intersection.left, intersection.top,
                                   intersection.right - intersection.left,
                                   intersection.bottom - intersection.top});
    result.reliable = !result.occupied.empty();
    std::sort(result.occupied.begin(), result.occupied.end(), [](Rect a, Rect b) { return a.x < b.x; });
    return result;
}
// Whether two readings differ enough to be worth acting on. Exact comparison
// made every one-pixel reflow of a taskbar label look like a layout change.
static bool settled(const TaskbarSnapshot &a, const TaskbarSnapshot &b)
{
    auto close = [](Rect x, Rect y)
    {
        return std::abs(x.x - y.x) <= TaskbarObserver::tolerance &&
               std::abs(x.y - y.y) <= TaskbarObserver::tolerance &&
               std::abs(x.w - y.w) <= TaskbarObserver::tolerance &&
               std::abs(x.h - y.h) <= TaskbarObserver::tolerance;
    };
    if (a.reliable != b.reliable || a.occupied.size() != b.occupied.size() || !close(a.bounds, b.bounds))
        return false;
    for (size_t i = 0; i < a.occupied.size(); ++i)
        if (!close(a.occupied[i], b.occupied[i]))
            return false;
    return true;
}
TaskbarObserver::TaskbarObserver()
{
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    refresh_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}
TaskbarObserver::~TaskbarObserver()
{
    stop();
    if (stop_)
        CloseHandle(stop_);
    if (refresh_)
        CloseHandle(refresh_);
}
void TaskbarObserver::start(HWND notify, UINT message)
{
    if (!stop_ || !refresh_ || thread_.joinable())
        return;
    ResetEvent(stop_);
    thread_ = std::thread(
        [this, notify, message]
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            {
                ComPtr<IUIAutomation> automation;
                CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&automation));
                ComPtr<IUIAutomation2> bounded;
                if (automation && SUCCEEDED(automation.As(&bounded)))
                {
                    bounded->put_ConnectionTimeout(500);
                    bounded->put_TransactionTimeout(500);
                }
                do
                {
                    if (enabled_)
                    {
                        auto next = readTaskbar(automation.Get());
                        bool changed = false;
                        {
                            std::lock_guard lock(mutex_);
                            changed = !settled(next, latest_);
                            latest_ = std::move(next);
                        }
                        if (changed)
                            PostMessageW(notify, message, 0, 0);
                    }
                    // The first wait is a floor on how often Explorer's tree can
                    // be walked however busy the shell is; the second is the
                    // idle cadence, cut short when a refresh is requested.
                    if (WaitForSingleObject(stop_, minimumIntervalMs) == WAIT_OBJECT_0)
                        break;
                    HANDLE waits[] = {stop_, refresh_};
                    if (WaitForMultipleObjects(2, waits, FALSE, idleIntervalMs - minimumIntervalMs) ==
                        WAIT_OBJECT_0)
                        break;
                } while (true);
            }
            CoUninitialize();
        });
}
void TaskbarObserver::enable(bool on)
{
    const bool was = enabled_.exchange(on);
    if (on && !was)
        request();
}
void TaskbarObserver::request()
{
    if (refresh_)
        SetEvent(refresh_);
}
void TaskbarObserver::stop()
{
    if (thread_.joinable())
    {
        SetEvent(stop_);
        thread_.join();
    }
}
TaskbarSnapshot TaskbarObserver::snapshot()
{
    std::lock_guard lock(mutex_);
    return latest_;
}
} // namespace perf
