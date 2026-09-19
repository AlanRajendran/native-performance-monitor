#include "cpu.h"
#include <algorithm>
#include <cstring>
#include <intrin.h>
#include <set>
namespace perf
{
std::optional<std::pair<unsigned, unsigned>> parseCpuInstance(std::wstring_view v)
{
    auto comma = v.find(L',');
    if (comma == v.npos || comma == 0 || comma + 1 == v.size())
        return {};
    auto number = [](std::wstring_view s, unsigned max) -> std::optional<unsigned>
    {
        unsigned n = 0;
        for (auto c : s)
        {
            if (c < L'0' || c > L'9')
                return {};
            n = n * 10 + unsigned(c - L'0');
            if (n > max)
                return {};
        }
        return n;
    };
    auto g = number(v.substr(0, comma), 65535), p = number(v.substr(comma + 1), 63);
    if (!g || !p)
        return {};
    return std::pair{*g, *p};
}
double coreUtilization(const CpuCore &core, const std::map<std::pair<unsigned, unsigned>, double> &values)
{
    if (core.logical.empty())
        return missing;
    double total = 0;
    for (auto key : core.logical)
    {
        auto it = values.find(key);
        if (it == values.end() || !valid(it->second))
            return missing;
        total += std::min(100.0, it->second);
    }
    return total / core.logical.size();
}
std::vector<CpuCore> enumerateCpuCores()
{
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (!bytes || bytes > 1024 * 1024)
        return {};
    std::vector<BYTE> data(bytes);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()),
            &bytes))
        return {};
    std::vector<CpuCore> result;
    std::set<unsigned> classes;
    for (DWORD at = 0; at < bytes;)
    {
        auto r = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data() + at);
        if (r->Size < 48 || at + r->Size > bytes)
            return {};
        if (r->Processor.GroupCount < 1 || 32U + 16U * r->Processor.GroupCount > r->Size)
            return {};
        CpuCore core;
        core.id = unsigned(result.size());
        core.efficiencyClass = r->Processor.EfficiencyClass;
        for (WORD i = 0; i < r->Processor.GroupCount; ++i)
        {
            auto g = r->Processor.GroupMask[i];
            for (unsigned bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit)
                if (g.Mask & (KAFFINITY(1) << bit))
                    core.logical.emplace_back(g.Group, bit);
        }
        classes.insert(core.efficiencyClass);
        result.push_back(std::move(core));
        at += r->Size;
    }
    for (auto &c : result)
    {
        if (classes.size() == 2)
            c.kind = c.efficiencyClass == *classes.rbegin() ? L"Performance" : L"Efficiency";
        else if (classes.size() > 2)
            c.kind = L"Class " + std::to_wstring(c.efficiencyClass);
        else
            c.kind = L"CPU";
    }
    std::stable_sort(result.begin(), result.end(),
                     [](auto &a, auto &b) { return a.efficiencyClass > b.efficiencyClass; });
    return result;
}
std::wstring cpuBrand()
{
    int r[4];
    __cpuid(r, 0x80000000);
    if (unsigned(r[0]) < 0x80000004)
        return L"CPU";
    char name[49]{};
    for (int i = 0; i < 3; ++i)
    {
        __cpuid(r, int(0x80000002U) + i);
        memcpy(name + i * 16, r, 16);
    }
    std::string n(name);
    auto first = n.find_first_not_of(' ');
    if (first != n.npos)
        n.erase(0, first);
    return std::wstring(n.begin(), n.end());
}
float panelContentHeight(const std::vector<CpuCore> &cores)
{
    std::set<unsigned> groups;
    for (auto &c : cores)
        groups.insert(c.efficiencyClass);
    // Mirrors the panel layout in render.cpp: fixed sections plus one heat row
    // per core, with a small gap between core types.
    if (cores.empty())
        return 565.f;
    const float pitch = cores.size() > 24 ? 8.f : 12.f, kinds = float(groups.size());
    return 541.f + float(cores.size()) * pitch - 2.f * kinds + 10.f * (kinds - 1);
}
} // namespace perf
