#pragma once
#include "collector.h"
#include <d2d1.h>
#include <dwrite.h>
#include <filesystem>
#include <wrl/client.h>

namespace perf
{
struct Palette
{
    D2D1_COLOR_F surface, text, muted, border, grid;
    std::array<D2D1_COLOR_F, 4> series;
    bool highContrast = false;
};
Palette palette(bool dark, bool highContrast = false);
struct BitmapSurface
{
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    void *pixels = nullptr;
    int width = 0, height = 0;
    ~BitmapSurface();
    bool resize(int w, int h);
    void clear();
    bool save(const std::filesystem::path &path) const;
};
class Renderer
{
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> textFactory_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dcTarget_;
    std::map<std::pair<int, bool>, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis_;

  public:
    bool initialize();
    ID2D1Factory *factory()
    {
        return factory_.Get();
    }
    void discard()
    {
        dcTarget_.Reset();
    }
    HRESULT drawBitmap(BitmapSurface &bitmap, float dpi, const Snapshot &s, const Palette &p, bool strip,
                       bool locked, float scroll = 0);
    HRESULT drawTarget(ID2D1RenderTarget *target, float width, float height, const Snapshot &s,
                       const Palette &p, bool strip, bool locked, bool mica, float scroll = 0);
    std::wstring accessibleText(const Snapshot &s, bool strip) const;

  private:
    IDWriteTextFormat *format(float size, bool strong = false);
};
Snapshot demonstrationSnapshot();
} // namespace perf
