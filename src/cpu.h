#pragma once
#include "core.h"
#include <windows.h>
namespace perf
{
struct CpuCore
{
    unsigned id = 0, efficiencyClass = 0;
    std::vector<std::pair<unsigned, unsigned>> logical;
    std::wstring kind;
    double current = missing;
    History history;
};
std::vector<CpuCore> enumerateCpuCores();
std::wstring cpuBrand();
std::optional<std::pair<unsigned, unsigned>> parseCpuInstance(std::wstring_view value);
double coreUtilization(const CpuCore &core, const std::map<std::pair<unsigned, unsigned>, double> &values);
} // namespace perf
