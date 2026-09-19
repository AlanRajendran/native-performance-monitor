#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
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
// A fixed ring of `historyPoints` values of any sample type with a `tick`
// member, indexed like Ring: gaps become default samples (tick -1), and a
// push for the tick already at the head replaces it.
template <class T> class TickRing
{
    std::array<T, historyPoints> slots_{};
    size_t next_ = 0, count_ = 0;
    int64_t last_ = INT64_MIN;
    void append(const T &v)
    {
        slots_[next_] = v;
        next_ = (next_ + 1) % historyPoints;
        count_ = std::min(count_ + 1, historyPoints);
    }

  public:
    void push(int64_t tick, T value)
    {
        value.tick = tick;
        if (count_ && tick == last_)
        {
            slots_[(next_ + historyPoints - 1) % historyPoints] = value;
            return;
        }
        if (count_ && tick < last_)
            clear();
        if (count_)
            for (int64_t gap = std::min<int64_t>(tick - last_ - 1, int64_t(historyPoints)); gap > 0; --gap)
                append(T{});
        append(value);
        last_ = tick;
    }
    std::array<T, historyPoints> ordered() const
    {
        std::array<T, historyPoints> out{};
        for (size_t i = 0; i < count_; ++i)
            out[historyPoints - count_ + i] = slots_[(next_ + historyPoints - count_ + i) % historyPoints];
        return out;
    }
    void clear()
    {
        slots_ = {};
        next_ = count_ = 0;
        last_ = INT64_MIN;
    }
};

// Who holds memory at one moment. Applications are identified by a hash of
// their key; bytes are floats, which is ample precision for display.
inline uint64_t ownerId(std::wstring_view key)
{
    return std::hash<std::wstring_view>{}(key);
}
struct MemoryOwner
{
    uint64_t id = 0;
    float ram = 0, vram = 0;
};
constexpr size_t memoryOwners = 12;
struct MemorySample
{
    int64_t tick = -1;
    float inUse = std::numeric_limits<float>::quiet_NaN(), cache = std::numeric_limits<float>::quiet_NaN(),
          free = std::numeric_limits<float>::quiet_NaN(), vramUsed = std::numeric_limits<float>::quiet_NaN();
    std::array<MemoryOwner, memoryOwners> owners{};
    unsigned count = 0;
    const MemoryOwner *find(uint64_t id) const
    {
        for (unsigned i = 0; i < count; ++i)
            if (owners[i].id == id)
                return &owners[i];
        return nullptr;
    }
};
// Seconds keep every sample; the hour view keeps the latest sample of each
// minute, which is what "who holds memory" means at that point in time.
class MemoryHistory
{
    TickRing<MemorySample> seconds_, minutes_;

  public:
    void push(int64_t second, const MemorySample &s)
    {
        seconds_.push(second, s);
        minutes_.push(second >= 0 ? second / 60 : (second - 59) / 60, s);
    }
    std::array<MemorySample, historyPoints> ordered(Range range) const
    {
        return range == Range::Minutes ? minutes_.ordered() : seconds_.ordered();
    }
    void clear()
    {
        seconds_.clear();
        minutes_.clear();
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
// The engine type in a GPU Engine instance name ("..._engtype_VideoDecode"),
// without the index some drivers append ("OFA_0" becomes "OFA").
std::wstring engineType(std::wstring_view instance);
// Display order and label for the engine types that carry user work. Types
// such as Security and VR are left out: they are idle for almost everyone.
std::optional<std::pair<int, std::wstring>> engineLabel(std::wstring_view type);
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
