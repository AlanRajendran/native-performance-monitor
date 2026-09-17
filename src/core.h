#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace perf
{
constexpr double missing = std::numeric_limits<double>::quiet_NaN();
inline bool valid(double n)
{
    return std::isfinite(n) && n >= 0;
}
using Metrics = std::array<double, 4>;
constexpr Metrics noMetrics{missing, missing, missing, missing};

// Both history ranges are stored as exactly this many points, so every graph
// draws the same number of samples whichever range the user selected.
constexpr size_t historyPoints = 60;

// The two selectable spans. Seconds is the live one-second trace; Minutes
// averages each wall-clock minute, giving a one-hour view at the same cost.
enum class Range
{
    Seconds,
    Minutes
};
constexpr int64_t rangeSeconds(Range r)
{
    return r == Range::Minutes ? 3600 : 60;
}

struct Sample
{
    int64_t second = -1;
    Metrics values = noMetrics;
};

// A fixed ring of `historyPoints` samples indexed by an absolute tick. The tick
// unit is whatever the owner counts in: seconds for the live trace, minutes for
// the hour trace. Gaps shorter than the ring are filled with missing samples so
// a pause leaves a visible break rather than a false straight line.
class Ring
{
    std::array<Sample, historyPoints> slots_{};
    size_t next_ = 0, count_ = 0;
    void append(Sample s);

  public:
    void push(int64_t tick, Metrics values);
    std::array<Sample, historyPoints> ordered() const;
    void clear();
    void clearMetric(size_t metric);
    size_t size() const
    {
        return count_;
    }
};

// Keeps the one-second and one-minute rings in step. Every sample feeds the
// live ring and accumulates into the current minute; the minute ring's newest
// point is the running average of the minute in progress, so the hour view
// advances continuously instead of stepping once every sixty seconds.
class History
{
    Ring seconds_, minutes_;
    int64_t bucket_ = INT64_MIN;
    std::array<double, 4> sums_{};
    std::array<unsigned, 4> counts_{};

  public:
    void push(int64_t second, Metrics values);
    std::array<Sample, historyPoints> ordered(Range range = Range::Seconds) const;
    void clear();
    void clearGpu();
    size_t size(Range range = Range::Seconds) const
    {
        return range == Range::Minutes ? minutes_.size() : seconds_.size();
    }
};
struct EngineKey
{
    uint64_t adapter = 0;
    uint32_t physical = 0, engine = 0;
    auto operator<=>(const EngineKey &) const = default;
};
struct GpuInstance
{
    EngineKey key;
    uint32_t pid = 0;
    bool hasPid = false, hasEngine = false;
};
std::optional<GpuInstance> parseGpuInstance(std::wstring_view s);
double cpuPercent(uint64_t previous100ns, uint64_t current100ns, double elapsed, unsigned processors);
double capacityPercent(double bytes, double capacity);
std::wstring normalizePath(std::wstring_view path);
std::wstring fileName(std::wstring_view path);
bool isBaseWindowsProcess(std::wstring_view path, std::wstring_view windowsDir, uint32_t pid);

struct ProcessRow
{
    uint32_t pid = 0;
    uint64_t created = 0;
    std::wstring path, name;
    double cpu = missing, vram = missing, ram = missing;
    std::map<EngineKey, double> engines;
    bool gpuAvailable = false;
};
struct AppRow
{
    std::wstring key, name;
    unsigned count = 0;
    double cpu = missing, gpu = missing, vram = missing, ram = missing, pressure = missing, tie = 0;
};
std::vector<AppRow> rankApplications(const std::vector<ProcessRow> &rows, double vramCapacity,
                                     double ramCapacity);
double busiestEngine(const std::map<EngineKey, double> &engines);
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect &) const = default;
};
Rect clampRect(Rect value, Rect work, int minWidth, int minHeight);
std::optional<Rect> taskbarSlot(Rect bar, const std::vector<Rect> &occupied, int width, int height,
                                int preferredX, int margin);
std::wstring formatPercent(double n, bool decimal = false);
std::wstring formatBytes(double n, bool compact = false);
} // namespace perf
