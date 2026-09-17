#include "core.h"
#include <iostream>
#include <stdexcept>
using namespace perf;
static unsigned checks = 0;
static void require(bool condition, const char *name)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(name);
}
static bool near(double a, double b)
{
    return std::abs(a - b) < .00001;
}
int main()
{
    try
    {
        require(near(cpuPercent(0, 10000000, 1, 8), 12.5), "single busy thread normalized to whole machine");
        require(near(cpuPercent(0, 10000000, 1, 128), .78125), "more than 64 logical processors");
        require(near(cpuPercent(100, 30000100, 1.5, 4), 50), "actual irregular elapsed time");
        require(!valid(cpuPercent(10, 9, 1, 8)), "counter reset");
        require(!valid(cpuPercent(0, 1, 0, 8)), "zero interval");
        require(!valid(cpuPercent(0, 1, 1, 0)), "zero processors");
        require(!valid(capacityPercent(1, 0)), "UMA must not divide by zero");
        require(near(capacityPercent(4, 8), 50), "capacity fraction");
        require(!valid(capacityPercent(missing, 8)), "missing is not zero");
        History h;
        h.push(100, {1, 2, 3, 4});
        h.push(102, {5, 6, 7, 8});
        auto a = h.ordered();
        require(h.size() == 3, "gap consumes a time slot");
        require(a[58].second == 101 && !valid(a[58].values[0]), "gap does not interpolate data");
        require(a[59].values[0] == 5, "new sample");
        h.push(102, {9, 8, 7, 6});
        require(h.size() == 3 && h.ordered()[59].values[0] == 9, "duplicate second replaces");
        for (int i = 103; i < 180; ++i)
            h.push(i, {double(i), 2, 3, 4});
        a = h.ordered();
        require(h.size() == 60 && a[0].second == 120 && a[59].second == 179, "ring rollover");
        h.clearGpu();
        require(!valid(h.ordered()[0].values[1]) && valid(h.ordered()[0].values[0]),
                "GPU selection retains CPU history");
        h.push(300, {1, 2, 3, 4});
        require(h.size() == 1, "resume after a minute resets displayed window");
        h.push(1, {1, 2, 3, 4});
        require(h.size() == 1, "clock discontinuity");
        auto p = parseGpuInstance(L"pid_1234_luid_0x00000001_0x00aBcD09_phys_0_eng_2_engtype_3D");
        require(p && p->pid == 1234 && p->hasPid && p->hasEngine && p->key.adapter == 0x100abcd09ULL &&
                    p->key.engine == 2,
                "GPU instance identity");
        p = parseGpuInstance(L"luid_0x00000000_0x12345678_phys_1");
        require(p && !p->hasPid && !p->hasEngine && p->key.physical == 1, "adapter memory instance");
        for (auto s : {L"", L"pid_X_luid_0x0_0x1_phys_0_eng_1", L"luid_0x1_0x2_phys_99999999999999",
                       L"luid_0x100000000_0x2_phys_0", L"luid_0x0_0xZ_phys_0", L"luid_0x0_0x2_phys_0_eng_bad",
                       L"luid_0x0_0x2_phys_0garbage"})
            require(!parseGpuInstance(s), "malformed GPU instance rejected");
        require(normalizePath(L"C:/Apps/Code.exe") == normalizePath(L"\\\\?\\C:\\Apps\\CODE.EXE"),
                "path case and separator normalization");
        require(isBaseWindowsProcess(L"C:\\Windows\\System32\\svchost.exe", L"C:\\Windows", 55),
                "core Windows process excluded");
        require(!isBaseWindowsProcess(L"C:\\Apps\\svchost.exe", L"C:\\Windows", 55),
                "do not exclude by basename alone");
        require(!isBaseWindowsProcess(L"C:\\Windows\\System32\\notepad.exe", L"C:\\Windows", 55),
                "Notepad remains an app");
        require(!isBaseWindowsProcess(L"C:\\WindowsEvil\\System32\\svchost.exe", L"C:\\Windows", 55),
                "Windows directory component boundary");
        EngineKey e1{1, 0, 0}, e2{1, 0, 1};
        std::vector<ProcessRow> rows;
        rows.push_back(
            {10, 100, L"C:\\Apps\\Render.exe", L"Render", 10, 100, 500, {{e1, 30}, {e2, 5}}, true});
        rows.push_back({11, 101, L"c:/apps/RENDER.exe", L"Render", 5, 200, 100, {{e1, 10}, {e2, 45}}, true});
        auto top = rankApplications(rows, 1000, 2000);
        require(top.size() == 1 && top[0].count == 2, "executable subprocess aggregation");
        require(near(top[0].cpu, 15) && near(top[0].ram, 600) && near(top[0].vram, 300),
                "memory and CPU aggregation");
        require(near(top[0].gpu, 50), "GPU sum per engine before max, not sum of process maxima");
        require(near(top[0].pressure, 50), "dominant share ranking");
        rows.push_back({12, 100, L"D:\\Apps\\Render.exe", L"Render", 2, 800, 100, {}, true});
        top = rankApplications(rows, 1000, 2000);
        require(top.size() == 2 && top[0].key.starts_with(L"d:"),
                "separate full paths; VRAM-heavy app outranks CPU");
        rows[0].cpu = missing;
        top = rankApplications(rows, 1000, 2000);
        require(!valid(top[1].cpu) && valid(top[1].ram), "partial process data marks group CPU unavailable");
        rows.clear();
        for (int i = 0; i < 8; ++i)
        {
            ProcessRow r;
            r.pid = i + 10;
            r.path = L"C:\\Apps\\" + std::to_wstring(i) + L".exe";
            r.name = r.path;
            r.cpu = double(i);
            rows.push_back(r);
        }
        top = rankApplications(rows, 0, 0);
        require(top.size() == 5 && top.front().cpu == 7 && top.back().cpu == 3, "bounded top five");
        rows.clear();
        rows.push_back({50, 100, L"", L"Unknown", 1, missing, missing, {}, false});
        rows.push_back({50, 101, L"", L"Unknown", 1, missing, missing, {}, false});
        top = rankApplications(rows, 0, 0);
        require(top.size() == 2, "unknown paths and recycled PIDs not guessed into group");
        ProcessRow allMissing;
        allMissing.path = L"unknown";
        top = rankApplications({allMissing}, 0, 0);
        require(top.empty(), "wholly unavailable row is not ranked as zero");
        auto r = clampRect({2000, 1000, 420, 548}, {-1920, -100, 1920, 1080}, 360, 260);
        require(r.x == -420 && r.y == 432, "negative primary monitor origin");
        r = clampRect({0, 0, 1000, 1000}, {0, 0, 320, 200}, 360, 260);
        require(r.w == 320 && r.h == 200, "small screen clamps below nominal minimum");
        require(formatPercent(missing) == L"—" && formatBytes(missing) == L"—", "unavailable formatting");
        Rect bar{0, 1032, 1920, 48};
        std::vector<Rect> controls{{0, 1032, 170, 48}, {760, 1032, 420, 48}, {1580, 1032, 340, 48}};
        auto slot = taskbarSlot(bar, controls, 344, 42, 1500, 8);
        require(slot && *slot == Rect{1228, 1035, 344, 42}, "taskbar slot between apps and tray");
        require(!taskbarSlot(bar, {bar}, 344, 42, 1500, 8), "full taskbar has no overlay slot");
        require(!taskbarSlot({0, 0, 1920, 24}, {}, 344, 42, 0, 8), "short taskbar rejected");
        slot = taskbarSlot(bar, {{1600, 1032, 200, 48}, {1700, 1032, 220, 48}}, 344, 42, 1800, 8);
        require(slot && slot->x == 1248, "overlapping reservations merge");
        slot = taskbarSlot({-1920, 0, 1920, 48}, {{-340, 0, 340, 48}}, 344, 42, -100, 8);
        require(slot && slot->x == -692, "negative monitor origin taskbar slot");
        slot = taskbarSlot(bar, {{10000, 1032, 100, 48}, {0, -200, 1920, 48}}, 344, 42, 1900, 8);
        require(slot && slot->x == 1568, "off-monitor controls do not escape bounds");
        std::cout << checks << " core assertions passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
