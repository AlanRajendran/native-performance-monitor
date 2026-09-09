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
struct Sample
{
    int64_t second = -1;
    Metrics values = noMetrics;
};
class History
{
    std::array<Sample, 60> slots_{};
    size_t next_ = 0, count_ = 0;
    void append(Sample s);

  public:
    void push(int64_t second, Metrics values);
    std::array<Sample, 60> ordered() const;
    void clear();
    void clearGpu();
    size_t size() const
    {
        return count_;
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
