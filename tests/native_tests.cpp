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
        s.strip = false;
        require(saveSettings(dir, s, error), "atomic settings save");
        auto loaded = loadSettings(dir);
        require(loaded.panelX == 120 && loaded.adapter == UINT64_MAX && loaded.compact && !loaded.strip,
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
        Renderer renderer;
        require(renderer.initialize(), "Direct2D initialized");
        auto sample = demonstrationSnapshot();
        for (bool dark : {false, true})
            for (int dpi : {96, 120, 144, 192})
                for (bool strip : {false, true})
                {
                    BitmapSurface bitmap;
                    int width = strip ? 344 : 420, height = strip ? 42 : 548;
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
