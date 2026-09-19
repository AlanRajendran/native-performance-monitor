#include "core.h"
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <tuple>

namespace perf
{
void Ring::append(Sample s)
{
    slots_[next_] = s;
    next_ = (next_ + 1) % historyPoints;
    count_ = std::min(historyPoints, count_ + 1);
}
void Ring::clear()
{
    slots_ = {};
    next_ = count_ = 0;
}
void Ring::clearMetric(size_t metric)
{
    if (metric >= std::tuple_size_v<Metrics>)
        return;
    for (auto &s : slots_)
        s.values[metric] = missing;
}
void Ring::push(int64_t tick, Metrics values)
{
    if (count_)
    {
        auto &last = slots_[(next_ + historyPoints - 1) % historyPoints];
        if (tick == last.second)
        {
            last.values = values;
            return;
        }
        // A backwards jump or a gap wider than the ring makes every retained
        // sample unreachable, so start over rather than filling the whole ring.
        if (tick < last.second || tick - last.second > int64_t(historyPoints))
            clear();
        else
            for (auto t = last.second + 1; t < tick; ++t)
                append({t, noMetrics});
    }
    append({tick, values});
}
std::array<Sample, historyPoints> Ring::ordered() const
{
    std::array<Sample, historyPoints> result{};
    const auto start = (next_ + historyPoints - count_) % historyPoints;
    for (size_t i = 0; i < count_; ++i)
        result[historyPoints - count_ + i] = slots_[(start + i) % historyPoints];
    return result;
}
void History::clear()
{
    seconds_.clear();
    minutes_.clear();
    bucket_ = INT64_MIN;
    sums_ = {};
    counts_ = {};
}
void History::clearGpu()
{
    for (size_t metric : {size_t(1), size_t(2)})
    {
        seconds_.clearMetric(metric);
        minutes_.clearMetric(metric);
        sums_[metric] = 0;
        counts_[metric] = 0;
    }
}
void History::push(int64_t second, Metrics values)
{
    seconds_.push(second, values);
    // Floor division: negative seconds must not round the bucket towards zero.
    const int64_t minute = second >= 0 ? second / 60 : (second - 59) / 60;
    if (minute != bucket_)
    {
        bucket_ = minute;
        sums_ = {};
        counts_ = {};
    }
    Metrics average = noMetrics;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (valid(values[i]))
        {
            sums_[i] += values[i];
            ++counts_[i];
        }
        if (counts_[i])
            average[i] = sums_[i] / counts_[i];
    }
    // Rewriting the same minute each second keeps the newest hour point live.
    minutes_.push(minute, average);
}
std::array<Sample, historyPoints> History::ordered(Range range) const
{
    return range == Range::Minutes ? minutes_.ordered() : seconds_.ordered();
}
static bool number(std::wstring_view s, size_t &p, uint64_t &v, unsigned base)
{
    size_t begin = p;
    v = 0;
    while (p < s.size())
    {
        wchar_t c = s[p];
        unsigned d = c >= L'0' && c <= L'9'   ? unsigned(c - L'0')
                     : c >= L'a' && c <= L'f' ? unsigned(c - L'a' + 10)
                     : c >= L'A' && c <= L'F' ? unsigned(c - L'A' + 10)
                                              : 99;
        if (d >= base)
            break;
        if (v > (UINT64_MAX - d) / base)
            return false;
        v = v * base + d;
        ++p;
    }
    return p > begin && (p == s.size() || s[p] == L'_');
}
static std::optional<uint64_t> field(std::wstring_view s, std::wstring_view tag, unsigned base = 10)
{
    size_t p = s.find(tag);
    if (p == s.npos || (p && s[p - 1] != L'_'))
        return {};
    p += tag.size();
    uint64_t n = 0;
    if (base == 16 && s.substr(p, 2) == L"0x")
        p += 2;
    if (!number(s, p, n, base))
        return {};
    return n;
}
std::optional<GpuInstance> parseGpuInstance(std::wstring_view s)
{
    GpuInstance r;
    auto pos = s.find(L"luid_");
    if (pos == s.npos || (pos && s[pos - 1] != L'_'))
        return {};
    size_t p = pos + 5;
    uint64_t hi = 0, lo = 0;
    if (s.substr(p, 2) != L"0x")
        return {};
    p += 2;
    if (!number(s, p, hi, 16) || p >= s.size() || hi > UINT32_MAX)
        return {};
    ++p;
    if (s.substr(p, 2) != L"0x")
        return {};
    p += 2;
    if (!number(s, p, lo, 16) || lo > UINT32_MAX)
        return {};
    auto phys = field(s, L"phys_");
    if (!phys || *phys > UINT32_MAX)
        return {};
    r.key.adapter = (hi << 32) | lo;
    r.key.physical = uint32_t(*phys);
    if (s.find(L"pid_") != s.npos)
    {
        auto pid = field(s, L"pid_");
        if (!pid || *pid > UINT32_MAX)
            return {};
        r.pid = uint32_t(*pid);
        r.hasPid = true;
    }
    if (s.find(L"_eng_") != s.npos)
    {
        auto eng = field(s, L"eng_");
        if (!eng || *eng > UINT32_MAX)
            return {};
        r.key.engine = uint32_t(*eng);
        r.hasEngine = true;
    }
    return r;
}
double cpuPercent(uint64_t prev, uint64_t now, double elapsed, unsigned count)
{
    if (now < prev || elapsed <= 0 || !count)
        return missing;
    double n = double(now - prev) / 10000000.0 / elapsed / double(count) * 100.0;
    return n <= 100.5 ? std::clamp(n, 0.0, 100.0) : missing;
}
double capacityPercent(double n, double capacity)
{
    return valid(n) && valid(capacity) && capacity > 0 ? 100 * n / capacity : missing;
}
std::wstring normalizePath(std::wstring_view path)
{
    std::wstring result(path);
    if (result.starts_with(L"\\\\?\\"))
        result.erase(0, 4);
    for (auto &c : result)
        c = c == L'/' ? L'\\' : wchar_t(std::towlower(c));
    while (result.size() > 3 && result.back() == L'\\')
        result.pop_back();
    return result;
}
std::wstring fileName(std::wstring_view p)
{
    auto n = p.find_last_of(L"\\/");
    return std::wstring(n == p.npos ? p : p.substr(n + 1));
}
bool isBaseWindowsProcess(std::wstring_view path, std::wstring_view windowsDir, uint32_t pid)
{
    if (pid == 0 || pid == 4)
        return true;
    auto p = normalizePath(path), w = normalizePath(windowsDir);
    auto n = fileName(p);
    if (!(p.starts_with(w + L"\\system32\\") || p.starts_with(w + L"\\syswow64\\") ||
          p.starts_with(w + L"\\systemapps\\") || p == w + L"\\explorer.exe"))
        return false;
    static constexpr std::wstring_view names[] = {L"smss.exe",
                                                  L"csrss.exe",
                                                  L"wininit.exe",
                                                  L"services.exe",
                                                  L"lsass.exe",
                                                  L"winlogon.exe",
                                                  L"svchost.exe",
                                                  L"fontdrvhost.exe",
                                                  L"dwm.exe",
                                                  L"conhost.exe",
                                                  L"explorer.exe",
                                                  L"runtimebroker.exe",
                                                  L"searchindexer.exe",
                                                  L"searchhost.exe",
                                                  L"startmenuexperiencehost.exe",
                                                  L"shellexperiencehost.exe",
                                                  L"applicationframehost.exe",
                                                  L"sihost.exe",
                                                  L"ctfmon.exe",
                                                  L"taskhostw.exe",
                                                  L"dllhost.exe",
                                                  L"audiodg.exe",
                                                  L"secure system",
                                                  L"registry",
                                                  L"memory compression"};
    return std::find(std::begin(names), std::end(names), n) != std::end(names);
}
std::wstring engineType(std::wstring_view instance)
{
    auto at = instance.find(L"engtype_");
    if (at == instance.npos)
        return {};
    std::wstring type(instance.substr(at + 8));
    while (!type.empty() && iswdigit(type.back()))
        type.pop_back();
    while (!type.empty() && type.back() == L'_')
        type.pop_back();
    return type;
}
std::optional<std::pair<int, std::wstring>> engineLabel(std::wstring_view type)
{
    static const std::pair<const wchar_t *, const wchar_t *> known[] = {
        {L"3D", L"3D"},
        {L"Compute", L"Compute"},
        {L"Cuda", L"Compute"},
        {L"Copy", L"Copy"},
        {L"VideoEncode", L"Video encode"},
        {L"VideoDecode", L"Video decode"},
        {L"VideoProcessing", L"Video process"},
        {L"OFA", L"Optical flow"},
        {L"JPEG_Decode", L"JPEG decode"},
    };
    for (int i = 0; i < int(std::size(known)); ++i)
        if (type == known[i].first)
            return std::pair{i, std::wstring(known[i].second)};
    return std::nullopt;
}
double busiestEngine(const std::map<EngineKey, double> &engines)
{
    double top = 0;
    for (const auto &[k, n] : engines)
    {
        (void)k;
        if (valid(n))
            top = std::max(top, n);
    }
    return top <= 100.5 ? std::min(top, 100.0) : missing;
}
std::vector<AppRow> rankApplications(const std::vector<ProcessRow> &rows, double vramCapacity,
                                     double ramCapacity)
{
    struct Group
    {
        AppRow r;
        bool first = true;
        std::map<EngineKey, double> engines;
        bool gpu = true;
    };
    std::map<std::wstring, Group> groups;
    auto sum = [](double &target, double n) { target = valid(target) && valid(n) ? target + n : missing; };
    for (const auto &p : rows)
    {
        auto key = p.path.empty() ? L"pid:" + std::to_wstring(p.pid) + L":" + std::to_wstring(p.created)
                                  : normalizePath(p.path);
        auto &g = groups[key];
        if (g.first)
        {
            g.first = false;
            g.r.key = key;
            g.r.name = p.name;
            g.r.cpu = g.r.vram = g.r.ram = 0;
        }
        ++g.r.count;
        sum(g.r.cpu, p.cpu);
        sum(g.r.vram, p.vram);
        sum(g.r.ram, p.ram);
        g.gpu = g.gpu && p.gpuAvailable;
        for (const auto &[k, n] : p.engines)
            if (valid(n))
                g.engines[k] += n;
    }
    std::vector<AppRow> top;
    for (auto &[key, g] : groups)
    {
        (void)key;
        g.r.gpu = g.gpu ? busiestEngine(g.engines) : missing;
        const Metrics shares{g.r.cpu, g.r.gpu, capacityPercent(g.r.vram, vramCapacity),
                             capacityPercent(g.r.ram, ramCapacity)};
        for (auto n : shares)
            if (valid(n))
            {
                g.r.pressure = valid(g.r.pressure) ? std::max(g.r.pressure, n) : n;
                g.r.tie += n;
            }
        if (!valid(g.r.pressure))
            continue;
        auto compare = [](const AppRow &a, const AppRow &b)
        {
            if (a.pressure != b.pressure)
                return a.pressure > b.pressure;
            if (a.tie != b.tie)
                return a.tie > b.tie;
            return a.key < b.key;
        };
        auto pos = std::lower_bound(top.begin(), top.end(), g.r, compare);
        top.insert(pos, std::move(g.r));
        if (top.size() > 5)
            top.pop_back();
    }
    return top;
}
Rect clampRect(Rect r, Rect area, int minWidth, int minHeight)
{
    r.w = std::clamp(r.w, std::min(minWidth, area.w), area.w);
    r.h = std::clamp(r.h, std::min(minHeight, area.h), area.h);
    r.x = std::clamp(r.x, area.x, area.x + area.w - r.w);
    r.y = std::clamp(r.y, area.y, area.y + area.h - r.h);
    return r;
}
std::optional<Rect> taskbarSlot(Rect bar, const std::vector<Rect> &occupied, int width, int height,
                                int preferredX, int margin)
{
    if (bar.w < width + margin * 2 || bar.h < height || width <= 0 || height <= 0)
        return std::nullopt;
    std::vector<std::pair<int, int>> blocks;
    for (const auto &r : occupied)
        if (r.w > 0 && r.h > 0 && r.y < bar.y + bar.h && r.y + r.h > bar.y && r.x < bar.x + bar.w &&
            r.x + r.w > bar.x)
            blocks.emplace_back(std::max(bar.x, r.x - margin), std::min(bar.x + bar.w, r.x + r.w + margin));
    std::sort(blocks.begin(), blocks.end());
    blocks.emplace_back(bar.x + bar.w - margin, bar.x + bar.w);
    int left = bar.x + margin, bestDistance = INT_MAX;
    std::optional<Rect> best;
    for (auto [begin, end] : blocks)
    {
        if (begin - left >= width)
        {
            int x = std::clamp(preferredX, left, begin - width);
            int distance = std::abs(x - preferredX);
            if (distance < bestDistance)
            {
                best = Rect{x, bar.y + (bar.h - height) / 2, width, height};
                bestDistance = distance;
            }
        }
        left = std::max(left, end);
    }
    return best;
}
std::wstring formatPercent(double n, bool decimal)
{
    if (!valid(n))
        return L"—";
    wchar_t b[48];
    swprintf_s(b, decimal ? L"%.1f%%" : L"%.0f%%", n);
    return b;
}
std::wstring formatBytes(double n, bool compact)
{
    if (!valid(n))
        return L"—";
    wchar_t b[64];
    (void)compact;
    if (n >= 1000000000)
        swprintf_s(b, L"%.2f GB", n / 1000000000);
    else
        swprintf_s(b, L"%.0f MB", n / 1000000);
    return b;
}
} // namespace perf
