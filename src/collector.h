#pragma once
#include "core.h"
#include "cpu.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <pdh.h>
#include <thread>
#include <windows.h>

namespace perf
{
struct Adapter
{
    uint64_t luid = 0;
    std::wstring name;
    double dedicated = 0;
    bool primary = false;
};
// One engine type of the selected GPU (3D, Copy, Video decode…): the busiest
// engine of that type, as a percentage.
struct GpuEngine
{
    std::wstring type, label;
    int order = 0;
    double current = missing;
    History history;
};
struct Snapshot
{
    History history;
    // [0] paging in bytes per second, [1] memory bus busy percentage (NVML).
    History memoryMetrics;
    MemoryHistory memory;
    std::vector<GpuEngine> engines;
    double ramCache = missing, ramFree = missing, paging = missing, memoryBus = missing;
    bool memoryBusAvailable = false;
    std::vector<CpuCore> cores;
    std::wstring cpuName;
    Metrics current = noMetrics;
    double ramUsed = missing, ramTotal = missing, vramUsed = missing, vramTotal = missing;
    uint64_t adapter = 0, updatedMs = 0, rankUpdatedMs = 0;
    std::wstring gpuName = L"GPU unavailable", status = L"Starting counters…";
    std::vector<AppRow> apps;
    unsigned processes = 0;
    bool paused = false;
};
std::vector<Adapter> enumerateAdapters();
class Collector
{
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread thread_;
    HANDLE stop_ = nullptr;
    mutable std::mutex mutex_;
    Snapshot latest_;
    std::atomic<uint64_t> adapter_{0};
    std::atomic<bool> paused_{false}, reset_{false}, memoryBus_{false};

  public:
    Collector();
    ~Collector();
    Collector(const Collector &) = delete;
    void start(uint64_t adapter, HWND notify, UINT message);
    void stop();
    void selectAdapter(uint64_t id)
    {
        adapter_ = id;
        reset_ = true;
    }
    void pause(bool pause)
    {
        paused_ = pause;
        reset_ = true;
    }
    void reset()
    {
        reset_ = true;
    }
    void enableMemoryBus(bool on)
    {
        memoryBus_ = on;
    }
    Snapshot snapshot() const;
};
} // namespace perf
