#include "render.h"
#include "settings.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace perf;
static unsigned checks = 0;
static void require(bool c, const char *name)
{
    ++checks;
    if (!c)
        throw std::runtime_error(name);
}
int wmain(int argc, wchar_t **argv)
{
    if (argc < 2)
        return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try
    {
        auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        auto dir = root / L"owned-settings";
        std::wstring error;
        Settings s;
        s.panelX = 120;
        s.panelY = 70;
        s.adapter = UINT64_MAX;
        s.compact = true;
        s.opacity = 40;
        s.strip = false;
        require(saveSettings(dir, s, error), "atomic settings save");
        auto loaded = loadSettings(dir);
        require(loaded.panelX == 120 && loaded.adapter == UINT64_MAX && loaded.compact && !loaded.strip &&
                    loaded.opacity == 40,
                "settings round trip including 64-bit LUID");
        require(std::filesystem::exists(dir / L".nativeperf-settings") &&
                    !std::filesystem::exists(dir / L"settings.ini.new"),
                "settings ownership and atomic rename");
        require(startupCommand(L"C:\\Apps With Spaces\\PerfMonitor.exe") ==
                    L"\"C:\\Apps With Spaces\\PerfMonitor.exe\" --autostart",
                "quoted startup path");
        require(startupCommand(L"relative.exe").empty(), "relative startup path refused");
        require(startupCommand(L"C:\\" + std::wstring(270, L'x') + L"\\PerfMonitor.exe").empty(),
                "Run command limit");
        {
            std::ofstream f(dir / L"unrelated.txt");
            f << "keep";
        }
        require(prepareUninstall(executablePath(), dir, true, error), "isolated data removal");
        require(std::filesystem::exists(dir / L"unrelated.txt") &&
                    !std::filesystem::exists(dir / L"settings.ini"),
                "unrelated file preserved");
        auto unowned = root / L"unowned";
        std::filesystem::create_directories(unowned);
        {
            std::ofstream f(unowned / L"settings.ini");
            f << "other app";
        }
        require(!prepareUninstall(executablePath(), unowned, true, error), "unowned data removal refused");
        require(std::filesystem::exists(unowned / L"settings.ini"), "unowned settings preserved");
        {
            // A new release line carries the previous line's settings across
            // once, reading them without touching the previous line's files.
            auto lines = root / L"lines";
            std::filesystem::remove_all(lines);
            auto previous = lines / L"NativePerfMonitor-1.4", current = lines / L"NativePerfMonitor-1.5";
            std::filesystem::create_directories(previous);
            {
                std::ofstream marker(previous / L".nativeperf-settings");
                marker << "NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2-v1.4\n";
                std::ofstream ini(previous / L"settings.ini");
                ini << "opacity=37\nlongRange=1\nstripX=812\n";
            }
            Settings imported;
            require(importPreviousSettings(current, imported), "previous line's settings found");
            require(imported.opacity == 37 && imported.range == Range::Minutes && imported.stripX == 812,
                    "previous line's choices imported");
            require(!std::filesystem::exists(current), "import does not create or write the new directory");
            std::ofstream(previous / L".nativeperf-settings") << "some other application\n";
            Settings refused;
            require(!importPreviousSettings(current, refused),
                    "a directory another application owns is ignored");
        }
        Renderer renderer;
        require(renderer.initialize(), "Direct2D initialized");
        auto sample = demonstrationSnapshot();
        for (bool dark : {false, true})
            for (int dpi : {96, 120, 144, 192})
                for (bool strip : {false, true})
                {
                    BitmapSurface bitmap;
                    int width = strip ? 344 : 450, height = strip ? 42 : 880;
                    require(bitmap.resize(width * dpi / 96, height * dpi / 96), "DPI bitmap allocation");
                    require(SUCCEEDED(
                                renderer.drawBitmap(bitmap, float(dpi), sample, palette(dark), strip, true)),
                            "native renderer at each DPI");
                    auto px = static_cast<const uint32_t *>(bitmap.pixels);
                    require((px[(height * dpi / 96 / 2) * bitmap.width + bitmap.width / 2] >> 24) > 0,
                            "render is not blank");
                    size_t translucent = 0, opaque = 0;
                    bool premultiplied = true;
                    for (int i = 0; i < bitmap.width * bitmap.height; ++i)
                    {
                        auto alpha = px[i] >> 24;
                        translucent += alpha > 0 && alpha < 240;
                        opaque += alpha == 255;
                        premultiplied = premultiplied && (px[i] & 255) <= alpha &&
                                        ((px[i] >> 8) & 255) <= alpha && ((px[i] >> 16) & 255) <= alpha;
                    }
                    require(translucent > size_t(bitmap.width * bitmap.height / 2) && opaque > 0,
                            "translucent background with opaque readable foreground");
                    require(premultiplied, "layered surface preserves premultiplied alpha");
                    if (dpi == 192)
                        require(bitmap.save(root / (std::wstring(strip ? L"strip-" : L"panel-") +
                                                    (dark ? L"dark.png" : L"light.png"))),
                                "native PNG export");
                }
        for (auto size : {std::pair{360, 460}, std::pair{360, 310}})
        {
            BitmapSurface b;
            require(b.resize(size.first, size.second), "compact allocation");
            require(SUCCEEDED(renderer.drawBitmap(b, 96, sample, palette(false), false, false,
                                                  size.second < 460 ? 150.f : 0.f)),
                    "compact and scrolled render");
            require(b.save(root / (size.second < 460 ? L"scrolled.png" : L"compact.png")), "compact export");
        }
        require(formatBytes(1000000000.) == L"1.00 GB", "decimal gigabyte boundary");
        require(formatBytes(1073741824.) == L"1.07 GB", "binary input numerically converted to GB");
        require(formatBytes(536870912.) == L"537 MB", "binary input numerically converted to MB");
        require(formatBytes(1000000.) == L"1 MB", "decimal megabyte boundary");
        require(formatBytes(999999999.) == L"1000 MB", "rounding below GB boundary remains MB");
        require(formatBytes(1000000000., true) == L"1.00 GB",
                "compact display retains explicit decimal units");
        require(std::abs(palette(true).surface.a - .25f) < .001, "25 percent background opacity");
        require(palette(false, true).surface.a == 1, "high contrast remains readable");
        require(parseCpuInstance(L"2,63") == std::pair<unsigned, unsigned>{2, 63},
                "processor group retained");
        require(!parseCpuInstance(L"0,_Total") && !parseCpuInstance(L"_Total") &&
                    !parseCpuInstance(L"0,64") && !parseCpuInstance(L"1,2x"),
                "aggregate and malformed counter instances ignored");
        CpuCore smt;
        smt.logical = {{0, 0}, {0, 1}};
        std::map<std::pair<unsigned, unsigned>, double> values{{{0, 0}, 100}, {{0, 1}, 0}};
        require(coreUtilization(smt, values) == 50, "SMT sibling mean");
        values.erase({0, 1});
        require(!valid(coreUtilization(smt, values)), "missing sibling is not zero");
        auto cores = enumerateCpuCores();
        require(!cores.empty(), "real physical core discovery");
        unsigned logical = 0;
        for (auto &c : cores)
            logical += unsigned(c.logical.size());
        require(logical == GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
                "all logical processors represented exactly");
        auto accessibility = renderer.accessibleText(sample, false);
        require(accessibility.find(L"Core 13 Performance") != accessibility.npos &&
                    accessibility.find(L"Core 3 Efficiency") != accessibility.npos,
                "typed core rows exposed to accessibility");
        require(accessibility.find(L"GiB") == accessibility.npos &&
                    accessibility.find(L"MiB") == accessibility.npos,
                "no binary unit labels in accessibility");
        s.opacity = 100;
        s.strip = true;
        require(saveSettings(dir, s, error), "save full opacity and enabled strip");
        auto full = loadSettings(dir);
        require(full.opacity == 100 && full.strip, "opaque endpoint and restored strip survive restart");
        auto adapters = enumerateAdapters();
        std::wcout << L"DXGI hardware adapters discovered: " << adapters.size() << L"\n";
        for (auto &a : adapters)
            std::wcout << a.name << L"; dedicated bytes " << a.dedicated << L"\n";
        std::cout << checks << " native assertions passed\n";
        CoUninitialize();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAILED: " << e.what() << "\n";
        CoUninitialize();
        return 1;
    }
}
