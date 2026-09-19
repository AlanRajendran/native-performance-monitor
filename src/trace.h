#pragma once
#include <filesystem>

namespace perf
{
// A timestamped log of placement decisions: every z-order change this program
// makes, every Show Desktop transition, every strip rebuild, and why.
//
// Three rounds of stability fixes were made by reasoning about symptoms. This
// exists so the next report comes with a record of what actually happened.
// It is off unless started with --trace or from the tray menu, and costs
// nothing while off. Safe to call from any thread.
namespace trace
{
void start(const std::filesystem::path &file);
void stop();
bool enabled();
void line(const wchar_t *format, ...);
} // namespace trace
} // namespace perf
