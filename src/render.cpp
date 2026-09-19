#include "render.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <wincodec.h>

namespace perf
{
using Microsoft::WRL::ComPtr;
enum class Align
{
    Left,
    Center,
    Right
};
static D2D1_COLOR_F color(UINT32 rgb, float a = 1)
{
    return D2D1::ColorF(rgb, a);
}
// Blends two opaque colours. Used instead of alpha so that subtle marks stay
// fully opaque: a 14%-of-text grid line is a solid colour, not a see-through one.
static D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t)
{
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.f);
}
// Windows keeps eight shades of the accent colour (which follows the wallpaper
// when "Automatic" is chosen) as RGBA quads, lightest first. Light2 is the
// shade Windows itself uses on dark surfaces and Dark1 on light ones.
static D2D1_COLOR_F systemAccent(bool dark)
{
    BYTE data[32]{};
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, nullptr, data, &size) == ERROR_SUCCESS &&
        size >= sizeof(data))
    {
        const BYTE *c = data + (dark ? 1 : 4) * 4;
        return D2D1::ColorF(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f);
    }
    return color(dark ? 0x4cc2ff : 0x0067c0);
}
Palette palette(bool dark, bool high)
{
    Palette p;
    if (dark)
    {
        p.surface = color(0x252930);
        p.plot = color(0x2c313a);
        p.text = color(0xf3f4f7);
        p.muted = color(0xb2bac6);
        p.series = {color(0x65bded), color(0xba9be7), color(0x62cbd1), color(0xeab561)};
    }
    else
    {
        p.surface = color(0xf6f7f9);
        p.plot = color(0xffffff);
        p.text = color(0x1c1e22);
        p.muted = color(0x5c626b);
        p.series = {color(0x0077b5), color(0x8056b7), color(0x007e83), color(0xb66e13)};
    }
    p.grid = mix(p.plot, p.text, .14f);
    p.border = mix(p.surface, p.text, .30f);
    for (size_t i = 0; i < p.series.size(); ++i)
        p.fill[i] = mix(p.plot, p.series[i], .18f);
    p.accent = systemAccent(dark);
    p.glow = dark ? mix(p.accent, color(0xffffff), .45f) : mix(p.accent, color(0x000000), .3f);
    p.heatBase = mix(p.surface, p.text, dark ? .06f : .08f);
    p.off = color(dark ? 0x33373e : 0xd3d8df);
    // Curated app colours: distinct hues at one lightness, calm on either theme.
    p.owners = dark ? std::array{color(0x7ea9d8), color(0xd6a35e), color(0x94c39a)}
                    : std::array{color(0x3f6fa6), color(0xa8742b), color(0x4f8a5b)};
    p.freeSpace = p.heatBase;
    p.cache = mix(p.heatBase, p.text, dark ? .16f : .14f);
    p.others = mix(p.heatBase, p.text, dark ? .46f : .40f);
    // The caller replaces this with the configured opacity; everything else
    // stays at full alpha so content never washes out into the wallpaper.
    p.surface.a = .25f;
    if (high)
    {
        auto cv = [](int id)
        {
            auto c = GetSysColor(id);
            return D2D1::ColorF(float(GetRValue(c)) / 255, float(GetGValue(c)) / 255,
                                float(GetBValue(c)) / 255);
        };
        p.surface = p.plot = cv(COLOR_WINDOW);
        p.text = p.muted = cv(COLOR_WINDOWTEXT);
        p.border = p.grid = p.text;
        for (auto &c : p.series)
            c = p.text;
        for (auto &c : p.fill)
            c = p.plot;
        p.accent = p.glow = p.text;
        p.heatBase = p.off = p.freeSpace = p.plot;
        p.owners = {p.text, p.text, p.text};
        p.others = p.cache = p.text;
        p.highContrast = true;
    }
    return p;
}
BitmapSurface::~BitmapSurface()
{
    clear();
}
void BitmapSurface::clear()
{
    if (dc)
    {
        if (old)
            SelectObject(dc, old);
        if (bitmap)
            DeleteObject(bitmap);
        DeleteDC(dc);
    }
    dc = nullptr;
    bitmap = nullptr;
    old = nullptr;
    pixels = nullptr;
    width = height = 0;
}
bool BitmapSurface::resize(int w, int h)
{
    if (w == width && h == height && pixels)
        return true;
    clear();
    if (w < 1 || h < 1 || w > 8192 || h > 8192)
        return false;
    dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap)
    {
        clear();
        return false;
    }
    old = SelectObject(dc, bitmap);
    width = w;
    height = h;
    return true;
}
bool BitmapSurface::save(const std::filesystem::path &path) const
{
    if (!pixels)
        return false;
    ComPtr<IWICImagingFactory> f;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))))
        return false;
    if (FAILED(f->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(enc->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height)))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)))
        return false;
    ComPtr<IWICBitmap> bitmapSource;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(f->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4,
                                         width * height * 4, static_cast<BYTE *>(pixels), &bitmapSource)) ||
        FAILED(f->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(bitmapSource.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    return SUCCEEDED(frame->WriteSource(converter.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
           SUCCEEDED(enc->Commit());
}
// The first installed family wins; the last entry is the one every Windows has.
static std::wstring installedFamily(IDWriteFactory *f, std::initializer_list<const wchar_t *> names)
{
    ComPtr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(f->GetSystemFontCollection(&fonts)))
        for (auto name : names)
        {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists)
                return name;
        }
    return *(names.end() - 1);
}
bool Renderer::initialize()
{
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(textFactory_.GetAddressOf()))))
        return false;
    factory_->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND),
        nullptr, 0, &roundCap_);
    text_ = installedFamily(textFactory_.Get(), {L"Segoe UI Variable Text", L"Segoe UI"});
    display_ = installedFamily(textFactory_.Get(), {L"Segoe UI Variable Display", L"Segoe UI"});
    mono_ = installedFamily(textFactory_.Get(), {L"Cascadia Mono", L"Consolas"});
    return true;
}
IDWriteTextFormat *Renderer::format(float size, DWRITE_FONT_WEIGHT weight, bool mono)
{
    auto key = std::tuple{int(size * 10), int(weight), mono};
    auto &f = formats_[key];
    if (!f)
    {
        const auto &family = mono ? mono_ : size >= 20 ? display_ : text_;
        textFactory_->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
        if (f)
        {
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            ComPtr<IDWriteInlineObject> ellipsis;
            textFactory_->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
            f->SetTrimming(&trim, ellipsis.Get());
        }
    }
    return f.Get();
}
HRESULT Renderer::drawBitmap(BitmapSurface &b, float dpi, const Snapshot &s, const Palette &p, bool strip,
                             bool locked, float scroll, Range range, bool embedded)
{
    if (!dcTarget_)
    {
        auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        auto hr = factory_->CreateDCRenderTarget(&props, &dcTarget_);
        if (FAILED(hr))
            return hr;
    }
    RECT r{0, 0, b.width, b.height};
    auto hr = dcTarget_->BindDC(b.dc, &r);
    if (FAILED(hr))
        return hr;
    dcTarget_->SetDpi(dpi, dpi);
    hr = drawTarget(dcTarget_.Get(), b.width * 96.f / dpi, b.height * 96.f / dpi, s, p, strip, locked, scroll,
                    range, embedded);
    if (hr == D2DERR_RECREATE_TARGET)
    {
        dcTarget_.Reset();
        staticLayer_.Reset();
    }
    return hr;
}
// Low load sits close to `base` so an idle machine reads as calm; load climbs
// towards the accent and the last 15% brightens past it, so peaks stand out
// even though every step is the same hue.
static D2D1_COLOR_F heat(const Palette &p, D2D1_COLOR_F base, double v)
{
    const float t = float(std::clamp(v, 0.0, 100.0)) / 100.f;
    return t < .85f ? mix(base, p.accent, std::pow(t / .85f, .8f)) : mix(p.accent, p.glow, (t - .85f) / .15f);
}
// Brand names are long and the gauges are narrow; the model is what matters.
static std::wstring shortName(std::wstring n)
{
    if (auto at = n.find(L" @"); at != n.npos)
        n.erase(at);
    for (const wchar_t *junk : {L"(R)", L"(r)", L"(TM)", L"(tm)", L" CPU", L" Processor"})
        for (size_t at; (at = n.find(junk)) != n.npos;)
            n.erase(at, wcslen(junk));
    for (const wchar_t *prefix : {L"Intel ", L"AMD ", L"NVIDIA ", L"GeForce "})
        if (n.rfind(prefix, 0) == 0)
            n.erase(0, wcslen(prefix));
    for (size_t at; (at = n.find(L"  ")) != n.npos;)
        n.erase(at, 1);
    return n;
}
static std::wstring percentText(double v)
{
    if (!valid(v))
        return L"—";
    wchar_t b[16];
    swprintf_s(b, v >= 10 ? L"%.0f%%" : L"%.1f%%", std::min(v, 100.0));
    return b;
}
static std::wstring sizeText(double bytes)
{
    if (!valid(bytes))
        return L"—";
    wchar_t b[16];
    swprintf_s(b, bytes >= 1e9 ? L"%.1f GB" : L"%.0f MB", bytes >= 1e9 ? bytes / 1e9 : bytes / 1e6);
    return b;
}
// Performance cores first, then efficiency cores, each in id order.
static std::vector<size_t> coreOrder(const std::vector<CpuCore> &cores)
{
    std::vector<size_t> order(cores.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return cores[a].efficiencyClass > cores[b].efficiencyClass; });
    return order;
}
// Text layouts are cached: the panel draws the same labels every second, and
// building a layout is most of what drawing a string costs.
IDWriteTextLayout *Renderer::layout(const std::wstring &v, float size, DWRITE_FONT_WEIGHT weight, bool mono,
                                    float width, int align)
{
    std::wstring key = v;
    key += L'\x1f';
    key += std::to_wstring(int(size * 10)) + L'.' + std::to_wstring(int(weight)) + (mono ? L"m" : L"s") +
           std::to_wstring(int(width * 4)) + L'.' + std::to_wstring(align);
    if (auto it = layouts_.find(key); it != layouts_.end())
        return it->second.Get();
    auto f = format(size, weight, mono);
    ComPtr<IDWriteTextLayout> l;
    if (!f || FAILED(textFactory_->CreateTextLayout(v.c_str(), UINT32(v.size()), f, std::max(width, 1.f),
                                                    size * 1.7f, &l)))
        return nullptr;
    l->SetTextAlignment(align == int(Align::Right)    ? DWRITE_TEXT_ALIGNMENT_TRAILING
                        : align == int(Align::Center) ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                      : DWRITE_TEXT_ALIGNMENT_LEADING);
    // Values change every second; keep the cache from growing without bound.
    if (layouts_.size() > 600)
        layouts_.clear();
    return (layouts_[key] = l).Get();
}
float Renderer::measure(const std::wstring &v, float size, DWRITE_FONT_WEIGHT weight, bool mono)
{
    auto l = layout(v, size, weight, mono, 4096, int(Align::Left));
    DWRITE_TEXT_METRICS m{};
    return l && SUCCEEDED(l->GetMetrics(&m)) ? m.widthIncludingTrailingWhitespace : 0;
}
// A block of pixels in device space. Heat maps, memory bars and block grids
// are written here directly and drawn with one bitmap each, instead of a
// thousand separate rectangles every second.
namespace
{
struct Raster
{
    int x0 = 0, y0 = 0, w = 0, h = 0;
    float scale = 1;
    std::vector<uint32_t> px;
    Raster(float x, float y, float width, float height, float s) : scale(s)
    {
        x0 = int(std::lround(x * s));
        y0 = int(std::lround(y * s));
        w = std::max(1, int(std::lround((x + width) * s)) - x0);
        h = std::max(1, int(std::lround((y + height) * s)) - y0);
        px.assign(size_t(w) * size_t(h), 0);
    }
    // Device row of a DIP offset from the raster's top.
    int row(float dip) const
    {
        return int(std::lround(dip * scale));
    }
    void fill(int ax, int ay, int bx, int by, uint32_t c)
    {
        ax = std::clamp(ax, 0, w);
        bx = std::clamp(bx, 0, w);
        ay = std::clamp(ay, 0, h);
        by = std::clamp(by, 0, h);
        for (int y = ay; y < by; ++y)
            std::fill(px.begin() + size_t(y) * w + ax, px.begin() + size_t(y) * w + bx, c);
    }
};
uint32_t pixel(D2D1_COLOR_F c)
{
    auto channel = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    return 0xff000000u | channel(c.r) << 16 | channel(c.g) << 8 | channel(c.b);
}
} // namespace
HRESULT Renderer::drawTarget(ID2D1RenderTarget *t, float w, float h, const Snapshot &s, const Palette &p,
                             bool isStrip, bool locked, float scroll, Range range, bool embedded)
{
    ComPtr<ID2D1SolidColorBrush> brush;
    auto hr = t->CreateSolidColorBrush(p.text, &brush);
    if (FAILED(hr))
        return hr;
    constexpr auto regular = DWRITE_FONT_WEIGHT_NORMAL;
    auto set = [&](D2D1_COLOR_F c) { brush->SetColor(c); };
    auto box = [&](float x, float y, float bw, float bh, D2D1_COLOR_F c)
    {
        set(c);
        t->FillRectangle(D2D1::RectF(x, y, x + bw, y + bh), brush.Get());
    };
    auto text = [&](const std::wstring &v, float x, float y, float width, float size, D2D1_COLOR_F c,
                    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, Align align = Align::Left,
                    bool mono = false)
    {
        if (auto l = layout(v, size, weight, mono, width, int(align)))
        {
            set(c);
            t->DrawTextLayout({x, y}, l, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    };
    t->BeginDraw();
    t->SetTransform(D2D1::Matrix3x2F::Identity());
    t->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    t->Clear(D2D1::ColorF(0, 0));
    // Embedded in the taskbar there is no plate, no border and no rounded
    // corners: the bar's own material is the background, and anything drawn
    // behind the content would announce the strip as a separate window sitting
    // on top of it. Everywhere else the surface plate carries the opacity.
    const float radius = isStrip ? 7.f : 16.f;
    auto plate = [&](ID2D1RenderTarget *target, ID2D1SolidColorBrush *ink)
    {
        ink->SetColor(p.surface);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), radius, radius), ink);
        ink->SetColor(p.border);
        target->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(.5f, .5f, w - .5f, h - .5f), radius, radius), ink, .65f);
    };
    if (!embedded && isStrip)
        plate(t, brush.Get());
    const bool stale = !s.paused && s.updatedMs && GetTickCount64() > s.updatedMs + 4000,
               live = !s.paused && !stale;
    if (!isStrip)
    {
        // Everything that rarely changes (plate, titles, labels, legends, app
        // names) is drawn once into a cached layer. Each second the layer is
        // copied and only readings, arcs and heat images are drawn on top.
        // The key is the static content itself, so any change rebuilds it.
        float dpiX = 96, dpiY = 96;
        t->GetDpi(&dpiX, &dpiY);
        std::wstring key =
            std::to_wstring(int(w)) + L'x' + std::to_wstring(int(h)) + L'@' + std::to_wstring(int(dpiX)) +
            L'+' + std::to_wstring(int(scroll)) + L'|' + std::to_wstring(pixel(p.surface)) + L'.' +
            std::to_wstring(int(p.surface.a * 255)) + L'|' + std::to_wstring(pixel(p.border)) + L'|';
        layoutPanel(nullptr, nullptr, w, h, s, p, locked, range, Pass::Static, &key);
        if (!staticLayer_ || staticTarget_ != t || key != staticKey_)
        {
            staticLayer_.Reset();
            ComPtr<ID2D1BitmapRenderTarget> layer;
            ComPtr<ID2D1SolidColorBrush> ink;
            if (SUCCEEDED(t->CreateCompatibleRenderTarget(D2D1::SizeF(w, h), &layer)) &&
                SUCCEEDED(layer->CreateSolidColorBrush(p.text, &ink)))
            {
                layer->BeginDraw();
                layer->Clear(D2D1::ColorF(0, 0));
                layer->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
                plate(layer.Get(), ink.Get());
                layer->PushAxisAlignedClip(D2D1::RectF(0, 0, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                layer->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll));
                layoutPanel(layer.Get(), ink.Get(), w, h, s, p, locked, range, Pass::Static);
                layer->SetTransform(D2D1::Matrix3x2F::Identity());
                layer->PopAxisAlignedClip();
                if (SUCCEEDED(layer->EndDraw()))
                    layer->GetBitmap(&staticLayer_);
            }
            staticKey_ = key;
            staticTarget_ = t;
        }
        t->PushAxisAlignedClip(D2D1::RectF(0, 0, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (staticLayer_)
            t->DrawBitmap(staticLayer_.Get(), D2D1::RectF(0, 0, w, h), 1.f,
                          D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        t->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll));
        if (!staticLayer_)
        {
            t->SetTransform(D2D1::Matrix3x2F::Identity());
            plate(t, brush.Get());
            t->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll));
            layoutPanel(t, brush.Get(), w, h, s, p, locked, range, Pass::Static);
        }
        const float content = layoutPanel(t, brush.Get(), w, h, s, p, locked, range, Pass::Dynamic);
        t->SetTransform(D2D1::Matrix3x2F::Identity());
        t->PopAxisAlignedClip();
        if (content > h && !locked)
            text(L"Scroll for more", w - 125, h - 19, 105, 11, p.muted, regular, Align::Right);
        return t->EndDraw();
    }
    // Instrument strip: fixed-width readings; CPU shows one bar per physical
    // core, the others a twelve-step meter.
    auto order = coreOrder(s.cores);
    static const wchar_t *names[] = {L"CPU", L"GPU", L"VRAM", L"RAM"};
    const float pad = embedded ? 4.f : 10.f, gap = 12.f, cell = (w - 2 * pad - 3 * gap) / 4,
                top = std::floor((h - 26) / 2);
    auto reading = [&](int i) -> std::wstring
    {
        if (s.paused)
            return L"Ⅱ";
        if (!live || !valid(s.current[i]))
            return L"—";
        wchar_t b[16];
        swprintf_s(b, s.current[i] >= 99.95 ? L"%.0f" : L"%.1f", std::min(s.current[i], 100.0));
        return b;
    };
    text(names[0], pad, top - 1, cell, 9, p.muted, regular, Align::Left, true);
    text(reading(0), pad, top + 11, cell, 12.5f, p.accent, regular, Align::Left, true);
    if (!order.empty())
    {
        const float column = measure(L"00.0", 12.5f, regular, true) + 5, base = top + 27;
        unsigned breaks = 0;
        for (size_t k = 1; k < order.size(); ++k)
            breaks += s.cores[order[k]].efficiencyClass != s.cores[order[k - 1]].efficiencyClass;
        const float pitch = (cell - column - 3.f * float(breaks)) / float(order.size()),
                    bar = std::max(1.f, pitch - 1.f);
        float x = pad + column;
        for (size_t k = 0; k < order.size(); ++k)
        {
            const auto &c = s.cores[order[k]];
            if (k && c.efficiencyClass != s.cores[order[k - 1]].efficiencyClass)
                x += 3;
            const double v = live && valid(c.current) ? std::min(c.current, 100.0) : 0;
            const float bh = std::max(2.f, 22.f * float(v) / 100.f);
            box(x, base - bh, bar, bh, heat(p, p.off, v));
            x += pitch;
        }
    }
    for (int i = 1; i < 4; ++i)
    {
        const float x = pad + float(i) * (cell + gap), segment = (cell - 11 * 1.5f) / 12;
        text(names[i], x, top + 3, cell, 9, p.muted, regular, Align::Left, true);
        text(reading(i), x, top - 1, cell, 12.5f, p.text, regular, Align::Right, true);
        const int lit = live && valid(s.current[i])
                            ? int(std::lround(std::clamp(s.current[i], 0.0, 100.0) / 100 * 12))
                            : 0;
        for (int j = 0; j < 12; ++j)
            box(x + float(j) * (segment + 1.5f), top + 20, segment, 6, j < lit ? p.accent : p.off);
    }
    return t->EndDraw();
}
float Renderer::panelHeight(const Snapshot &s, float width, Range range)
{
    return layoutPanel(nullptr, nullptr, width, 0, s, palette(true), true, range, Pass::Measure);
}
// The desktop panel. Every section shares one grid (label, one column per
// sample, value) so a moment in time lines up from the cores down to the
// memory bars. With `t` null nothing is drawn and only the height is worked
// out. Four text sizes: 28 gauge readings, 13 titles, 12 names and amounts,
// 11 everything secondary.
float Renderer::layoutPanel(ID2D1RenderTarget *t, ID2D1SolidColorBrush *brush, float w, float h,
                            const Snapshot &s, const Palette &p, bool locked, Range range, Pass pass,
                            std::wstring *key)
{
    // `paint`: pixels are produced. `walk`: positions are worked out, either to
    // paint or to record what the static layer would contain (`key`).
    const bool paint = t != nullptr, walk = paint || key != nullptr;
    const bool statics = pass != Pass::Dynamic, dynamics = pass != Pass::Static && pass != Pass::Measure;
    constexpr auto regular = DWRITE_FONT_WEIGHT_NORMAL, light = DWRITE_FONT_WEIGHT_LIGHT,
                   strong = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    constexpr float pad = 24, sectionGap = 24, labelW = 68, valueW = 40, gutter = 8, rowH = 10, rowPitch = 12,
                    streamH = 84, columnGap = 32;
    float dpi = 96;
    if (paint)
    {
        float dy = 96;
        t->GetDpi(&dpi, &dy);
    }
    const float scale = dpi / 96;
    const bool stale = !s.paused && s.updatedMs && GetTickCount64() > s.updatedMs + 4000,
               live = !s.paused && !stale;
    const bool pixels = paint && dynamics;
    auto set = [&](D2D1_COLOR_F c) { brush->SetColor(c); };
    // Static marks go on the cached layer; dynamic ones (`dyn`) are drawn
    // every second on top of it.
    auto wanted = [&](bool dyn) { return walk && (dyn ? dynamics : statics); };
    auto record = [&](const std::wstring &v, float x, float y, D2D1_COLOR_F c)
    {
        if (key)
            *key += v + L'@' + std::to_wstring(int(x * 8)) + L',' + std::to_wstring(int(y * 8)) + L'#' +
                    std::to_wstring(pixel(c)) + L';';
    };
    // `baseline` places text by its baseline, so mixed sizes on a row align.
    auto text = [&](const std::wstring &v, float x, float baseline, float width, float size, D2D1_COLOR_F c,
                    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, Align align = Align::Left,
                    bool dyn = false)
    {
        if (!wanted(dyn))
            return;
        if (!dyn)
            record(v, x, baseline, c);
        if (!paint)
            return;
        if (auto l = layout(v, size, weight, false, width, int(align)))
        {
            set(c);
            t->DrawTextLayout({x, baseline - 1.08f * size}, l, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    };
    auto rounded = [&](float x, float y, float bw, float bh, D2D1_COLOR_F c, float r)
    {
        if (!wanted(false))
            return;
        record(L"■", x, y, c);
        if (!paint)
            return;
        set(c);
        t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + bw, y + bh), r, r), brush);
    };
    auto blit = [&](const Raster &r)
    {
        ComPtr<ID2D1Bitmap> b;
        auto props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        if (SUCCEEDED(t->CreateBitmap(D2D1::SizeU(UINT32(r.w), UINT32(r.h)), r.px.data(), UINT32(r.w * 4),
                                      props, &b)))
            t->DrawBitmap(b.Get(),
                          D2D1::RectF(r.x0 / scale, r.y0 / scale, (r.x0 + r.w) / scale, (r.y0 + r.h) / scale),
                          1.f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    };
    // The track is static; the arc is redrawn every second.
    auto gauge = [&](float cx, float cy, float r, float stroke, double v)
    {
        if (wanted(false))
        {
            record(L"○", cx, cy, p.plot);
            if (paint)
            {
                set(p.plot);
                t->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), brush, stroke);
            }
        }
        if (!pixels || !valid(v))
            return;
        const float f = std::max(float(std::min(v, 100.0)) / 100.f, .01f);
        set(p.accent);
        if (f > .999f)
        {
            t->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), brush, stroke);
            return;
        }
        const float a = f * 6.2831853f;
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink)))
            return;
        sink->BeginFigure({cx, cy - r}, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment({cx + r * std::sin(a), cy - r * std::cos(a)}, {r, r}, 0,
                                      D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                      f > .5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        t->DrawGeometry(path.Get(), brush, stroke, roundCap_.Get());
    };
    auto title = [&](float x, float y, float cw, const std::wstring &name, const std::wstring &note,
                     bool dynamicNote = false)
    {
        text(name, x, y + 13, cw / 2, 13, p.text, strong);
        text(note, x + cw / 3, y + 13, cw * 2 / 3, 11, p.muted, regular, Align::Right, dynamicNote);
        return 29.f;
    };
    // One heat row per series; each row is 60 cells in the shared grid.
    struct HeatRow
    {
        std::wstring label, now;
        std::array<double, historyPoints> values;
        bool breakBefore = false, hot = false;
    };
    auto heatRows = [&](float x, float y, float cw, const std::vector<HeatRow> &rows)
    {
        float height = 0;
        for (auto &r : rows)
            height += (r.breakBefore ? 6.f : 0.f) + rowPitch;
        height -= rowPitch - rowH;
        if (!walk || rows.empty())
            return std::max(height, 0.f);
        const float cellsX = x + labelW + gutter, cellsW = cw - labelW - valueW - 2 * gutter;
        std::optional<Raster> r;
        if (pixels)
            r.emplace(cellsX, y, cellsW, height, scale);
        const float pitch = r ? float(r->w) / float(historyPoints) : 0;
        const int gap = std::max(1, int(std::lround(scale))),
                  tall = std::max(1, int(std::lround(rowH * scale)));
        float ry = 0;
        for (auto &row : rows)
        {
            ry += row.breakBefore ? 6.f : 0.f;
            if (r)
            {
                const int top = int(std::lround((y + ry) * scale)) - r->y0;
                for (size_t i = 0; i < historyPoints; ++i)
                    if (valid(row.values[i]))
                        r->fill(int(std::lround(float(i) * pitch)), top,
                                int(std::lround(float(i + 1) * pitch)) - gap, top + tall,
                                pixel(heat(p, p.heatBase, row.values[i])));
            }
            text(row.label, x, y + ry + 9, labelW, 11, p.muted);
            text(row.now, x + cw - valueW - gutter, y + ry + 9, valueW + gutter, 11,
                 row.hot ? p.accent : p.muted, regular, Align::Right, true);
            ry += rowPitch;
        }
        if (r)
            blit(*r);
        return height;
    };
    auto percent = [&](double v) { return live ? percentText(v) : std::wstring(L"—"); };

    // ---- Sections. Each takes its top-left and width and returns its height.
    auto header = [&](float x, float y, float cw)
    {
        text(L"This PC", x, y + 13, cw / 2, 13, p.text, strong);
        if (walk)
        {
            const std::wstring status = s.paused                  ? L"Paused · idle"
                                        : stale                   ? L"Stale · idle"
                                        : !locked                 ? L"Unlocked · idle"
                                        : range == Range::Minutes ? L"last hour · idle"
                                                                  : L"last minute · idle";
            const float busyW = measure(L"busy", 11), statusW = measure(status, 11);
            float rx = x + cw - busyW;
            text(L"busy", rx, y + 12, busyW + 2, 11, p.muted);
            rx -= 4 + 5 * 14 - 4;
            for (int k = 0; k < 5; ++k)
                rounded(rx + float(k) * 14, y + 3, 10, 10, heat(p, p.heatBase, k * 25.0), 2);
            text(status, rx - 8 - statusW, y + 12, statusW + 2, 11, p.muted);
        }
        return 16.f;
    };
    auto rings = [&](float x, float y, float cw)
    {
        const float col = (cw - 24) / 3, d = std::min(96.f, col - 8), stroke = d * 8 / 96;
        wchar_t memory[64] = L"—";
        if (valid(s.ramUsed) && valid(s.ramTotal) && s.ramTotal > 0)
            swprintf_s(memory, L"%.1f of %.1f GB", s.ramUsed / 1e9, s.ramTotal / 1e9);
        const struct
        {
            const wchar_t *name;
            int metric;
            std::wstring detail;
        } items[] = {
            {L"CPU", 0, shortName(s.cpuName)}, {L"GPU", 1, shortName(s.gpuName)}, {L"Memory", 3, memory}};
        for (int g = 0; g < 3; ++g)
        {
            const float cx = x + float(g) * (col + 12) + col / 2, cy = y + d / 2;
            const double v = live ? s.current[items[g].metric] : missing;
            gauge(cx, cy, (d - stroke) / 2, stroke, v);
            if (walk)
            {
                const float big = d * 28 / 96;
                const std::wstring value = valid(v)   ? std::to_wstring(int(std::lround(std::min(v, 100.0))))
                                           : s.paused ? L"Ⅱ"
                                                      : L"—";
                const float vw = measure(value, big, light), pw = valid(v) ? measure(L"%", 13) : 0;
                const float left = cx - (vw + pw) / 2, baseline = cy + .36f * big;
                text(value, left, baseline, vw + 2, big, p.text, light, Align::Left, true);
                if (pw)
                    text(L"%", left + vw, baseline, pw + 2, 13, p.muted, regular, Align::Left, true);
            }
            text(items[g].name, cx - col / 2, y + d + 21, col, 13, p.text, strong, Align::Center);
            text(items[g].detail.empty() ? L"—" : items[g].detail, cx - col / 2, y + d + 38, col, 11, p.muted,
                 regular, Align::Center, g == 2);
        }
        return d + 42;
    };
    auto cores = [&](float x, float y, float cw)
    {
        unsigned performance = 0, efficiency = 0;
        for (auto &c : s.cores)
        {
            performance += c.kind == L"Performance";
            efficiency += c.kind == L"Efficiency";
        }
        const bool hybrid = performance && efficiency;
        float height = title(x, y, cw, L"Cores",
                             hybrid ? std::to_wstring(performance) + L" performance · " +
                                          std::to_wstring(efficiency) + L" efficiency"
                                    : std::to_wstring(s.cores.size()) + L" cores");
        std::vector<HeatRow> rows;
        auto order = coreOrder(s.cores);
        for (size_t k = 0; k < order.size(); ++k)
        {
            const auto &c = s.cores[order[k]];
            HeatRow r;
            r.label = (hybrid ? std::wstring(1, c.kind[0]) : L"") + std::to_wstring(c.id);
            r.now = percent(c.current);
            r.hot = live && valid(c.current) && c.current > 30;
            r.breakBefore = k && c.efficiencyClass != s.cores[order[k - 1]].efficiencyClass;
            auto values = c.history.ordered(range);
            for (size_t i = 0; i < historyPoints; ++i)
                r.values[i] = values[i].values[0];
            rows.push_back(std::move(r));
        }
        if (rows.empty())
        {
            text(L"Waiting for CPU topology…", x, y + height + 9, cw, 11, p.muted);
            return height + rowH;
        }
        return height + heatRows(x, y + height, cw, rows);
    };
    const bool bus = s.memoryBusAvailable;
    auto engines = [&](float x, float y, float cw)
    {
        float height = title(x, y, cw, L"GPU engines", shortName(s.gpuName));
        std::vector<HeatRow> rows;
        for (auto &e : s.engines)
        {
            HeatRow r;
            r.label = e.label;
            r.now = percent(e.current);
            r.hot = live && valid(e.current) && e.current > 30;
            auto values = e.history.ordered(range);
            for (size_t i = 0; i < historyPoints; ++i)
                r.values[i] = values[i].values[0];
            rows.push_back(std::move(r));
        }
        if (bus)
        {
            HeatRow r;
            r.label = L"Memory bus*";
            r.now = percent(s.memoryBus);
            r.hot = live && valid(s.memoryBus) && s.memoryBus > 30;
            auto values = s.memoryMetrics.ordered(range);
            for (size_t i = 0; i < historyPoints; ++i)
                r.values[i] = values[i].values[1];
            rows.push_back(std::move(r));
        }
        if (rows.empty())
        {
            text(L"Waiting for GPU counters…", x, y + height + 9, cw, 11, p.muted);
            return height + rowH;
        }
        return height + heatRows(x, y + height, cw, rows);
    };
    // The three applications named in Working hardest own a colour each, the
    // same in the memory bars, the block grids and their tiles.
    std::array<uint64_t, 3> owners{};
    size_t ownerCount = 0;
    for (; ownerCount < s.apps.size() && ownerCount < 3; ++ownerCount)
        owners[ownerCount] = ownerId(s.apps[ownerCount].key);
    const auto memory = s.memory.ordered(range);
    auto pool = [&](float x, float y, float cw, bool ram)
    {
        const double total = ram ? s.ramTotal : s.vramTotal;
        // Parts from the bottom of a bar up: owners, everyone else, cache, free.
        auto parts = [&](const MemorySample &m, std::array<double, 6> &out) -> bool
        {
            const float used = ram ? m.inUse : m.vramUsed;
            if (m.tick < 0 || !std::isfinite(used) || !valid(total) || total <= 0)
                return false;
            double held = 0;
            for (size_t k = 0; k < 3; ++k)
            {
                const auto o = k < ownerCount ? m.find(owners[k]) : nullptr;
                out[k] = o ? double(ram ? o->ram : o->vram) : 0;
                held += out[k];
            }
            out[3] = std::max(0.0, double(used) - held);
            out[4] = ram && std::isfinite(m.cache) ? double(m.cache) : 0;
            out[5] = std::max(0.0, total - held - out[3] - out[4]);
            return true;
        };
        const D2D1_COLOR_F colors[] = {p.owners[0], p.owners[1], p.owners[2], p.others, p.cache, p.freeSpace};
        std::array<double, 6> now{}, first{};
        int latest = -1, earliest = -1;
        for (int i = int(historyPoints) - 1; i >= 0 && latest < 0; --i)
            if (parts(memory[size_t(i)], now))
                latest = i;
        for (int i = 0; i < int(historyPoints) && earliest < 0; ++i)
            if (parts(memory[size_t(i)], first))
                earliest = i;
        wchar_t note[64] = L"";
        if (latest >= 0)
            swprintf_s(note, L"%.1f of %.1f GB in use", (now[0] + now[1] + now[2] + now[3]) / 1e9,
                       total / 1e9);
        float height = title(x, y, cw, ram ? L"RAM" : L"VRAM", note, true);
        if (latest < 0)
        {
            text(ram ? L"Waiting for memory counters…" : L"Waiting for GPU memory counters…", x,
                 y + height + 9, cw, 11, p.muted);
            return height + rowH;
        }
        // The bars: one column per sample, split by who held the memory.
        const float cellsX = x + labelW + gutter, cellsW = cw - labelW - valueW - 2 * gutter,
                    top = y + height;
        wchar_t cap[32];
        swprintf_s(cap, L"%.0f GB", total / 1e9);
        text(cap, x, top + 9, labelW, 11, p.muted);
        text(L"0", x, top + streamH, labelW, 11, p.muted);
        if (pixels)
        {
            Raster r(cellsX, top, cellsW, streamH, scale);
            const float pitch = float(r.w) / float(historyPoints);
            const int gap = std::max(1, int(std::lround(scale)));
            for (size_t i = 0; i < historyPoints; ++i)
            {
                std::array<double, 6> v{};
                if (!parts(memory[i], v))
                    continue;
                const int ax = int(std::lround(float(i) * pitch)),
                          bx = int(std::lround(float(i + 1) * pitch)) - gap;
                double below = 0;
                int bottom = r.h;
                for (size_t k = 0; k < v.size(); ++k)
                {
                    below += v[k];
                    const int edge = r.h - int(std::lround(std::min(below / total, 1.0) * r.h));
                    // A gap separates each part from the one above it.
                    const int cut = k + 1 < v.size() ? gap : 0;
                    if (bottom - edge > cut)
                        r.fill(ax, edge + cut, bx, bottom, pixel(colors[k]));
                    bottom = edge;
                }
            }
            blit(r);
        }
        height += streamH;
        if (ram)
        {
            HeatRow paging;
            paging.label = L"Paging";
            auto values = s.memoryMetrics.ordered(range);
            // Reading 64 MB a second back from disk counts as the hottest cell.
            for (size_t i = 0; i < historyPoints; ++i)
                paging.values[i] =
                    valid(values[i].values[0]) ? values[i].values[0] / (64.0 * 1048576) * 100 : missing;
            wchar_t rate[32] = L"—";
            if (live && valid(s.paging))
                swprintf_s(rate, L"%.0f MB/s", s.paging / 1048576);
            paging.now = rate;
            height += 8 + heatRows(x, y + height + 8, cw, {paging});
        }
        height += 12;
        // Right now, as blocks: the smallest power-of-two size that keeps the
        // grid to about 144 blocks.
        double unit = 64.0 * 1048576;
        while (std::ceil(total / unit) > 144)
            unit *= 2;
        const int count = int(std::ceil(total / unit)), columns = 16,
                  blockRows = (count + columns - 1) / columns;
        const float blocksW = std::min(164.f, cw * .42f), blocksH = float(blockRows) * 10.5f - 1.5f,
                    gridTop = y + height;
        if (pixels)
        {
            Raster r(x, gridTop, blocksW, blocksH, scale);
            const float pitch = float(r.w) / float(columns);
            const int gap = std::max(1, int(std::lround(1.5f * scale))), tall = r.row(9);
            int placed = 0;
            for (size_t k = 0; k < now.size(); ++k)
            {
                const int n = k + 1 == now.size() ? count - placed : int(std::lround(now[k] / unit));
                for (int j = 0; j < n && placed < count; ++j, ++placed)
                {
                    const int c = placed % columns, row = placed / columns, ty = r.row(float(row) * 10.5f);
                    r.fill(int(std::lround(float(c) * pitch)), ty,
                           int(std::lround(float(c + 1) * pitch)) - gap, ty + tall, pixel(colors[k]));
                }
            }
            blit(r);
        }
        const std::wstring unitText = unit >= 1073741824 ? std::to_wstring(int(unit / 1073741824)) + L" GB"
                                                         : std::to_wstring(int(unit / 1048576)) + L" MB";
        text(L"each block is " + unitText, x, gridTop + blocksH + 6 + 11, blocksW + 60, 11, p.muted);
        // Legend: who, how much, and how that changed over the range.
        const float lx = x + blocksW + 20, lw = x + cw - lx;
        float ly = gridTop;
        static const wchar_t *fixed[] = {L"Windows, others", L"Cache", L"Free"};
        for (size_t k = 0; k < now.size(); ++k)
        {
            if (k < 3 && k >= ownerCount)
                continue;
            if (k == 4 && !ram)
                continue;
            rounded(lx, ly + 3, 10, 10, colors[k], 2);
            text(k < 3 ? s.apps[k].name : fixed[k - 3], lx + 18, ly + 12.5f, lw - 18 - 96, 12, p.text);
            text(sizeText(now[k]), lx + lw - 96, ly + 12.5f, 56, 12, p.text, regular, Align::Right, true);
            const double change = now[k] - first[k];
            wchar_t trend[24] = L"steady";
            if (std::abs(change) >= 5e7)
                swprintf_s(trend, L"%s%.1f", change > 0 ? L"+" : L"−", std::abs(change) / 1e9);
            text(trend, lx + lw - 36, ly + 12.5f, 36, 11, std::abs(change) >= 3e8 ? p.text : p.muted, regular,
                 Align::Right, true);
            ly += 20;
        }
        return height + std::max(blocksH + 6 + 15, ly - 4 - gridTop);
    };
    auto apps = [&](float x, float y, float cw)
    {
        const float valuesX = x + cw - 4 * 52;
        text(L"Working hardest", x, y + 13, valuesX - x, 13, p.text, strong);
        static const wchar_t *heads[] = {L"CPU", L"GPU", L"VRAM", L"RAM"};
        for (int k = 0; k < 4; ++k)
            text(heads[k], valuesX + float(k) * 52, y + 13, 52, 11, p.muted, regular, Align::Right);
        const float first = y + 29;
        if (s.apps.empty())
        {
            text(L"Waiting for application counters…", x, first + 9, cw, 11, p.muted);
            return 29 + 3 * 28.f + 2 * 12;
        }
        for (size_t i = 0; i < s.apps.size() && i < 3; ++i)
        {
            const auto &a = s.apps[i];
            const float ry = first + float(i) * 40;
            const std::array<double, 4> share{
                valid(a.cpu) ? a.cpu / 100 : -1, valid(a.gpu) ? a.gpu / 100 : -1,
                valid(a.vram) && valid(s.vramTotal) && s.vramTotal > 0 ? a.vram / s.vramTotal : -1,
                valid(a.ram) && valid(s.ramTotal) && s.ramTotal > 0 ? a.ram / s.ramTotal : -1};
            const auto top = int(std::max_element(share.begin(), share.end()) - share.begin());
            wchar_t initial = L'?';
            for (auto ch : a.name)
                if (iswalnum(ch))
                {
                    initial = wchar_t(towupper(ch));
                    break;
                }
            rounded(x, ry, 28, 28, mix(p.plot, p.owners[i], .3f), 8);
            text(std::wstring(1, initial), x, ry + 18.5f, 28, 12, p.owners[i], strong, Align::Center);
            const float nx = x + 40, nw = valuesX - 8 - nx;
            text(a.name, nx, ry + 11.5f, nw, 12, p.text);
            text(std::to_wstring(a.count) + (a.count == 1 ? L" process" : L" processes"), nx, ry + 25.5f, nw,
                 11, p.muted);
            const std::array<std::wstring, 4> values{percentText(a.cpu), percentText(a.gpu), sizeText(a.vram),
                                                     sizeText(a.ram)};
            for (int k = 0; k < 4; ++k)
                text(values[k], valuesX + float(k) * 52 - 8, ry + 18.5f, 60, 12,
                     k == top && share[top] >= 0 ? p.accent : p.text, regular, Align::Right, true);
        }
        return 29 + 3 * 28.f + 2 * 12;
    };

    // ---- Flow. One column when narrow; two when there is room side by side.
    const int columns = w >= 860 ? 2 : 1;
    const float cw = columns == 2 ? (w - 2 * pad - columnGap) / 2 : w - 2 * pad;
    using Section = std::function<float(float, float, float)>;
    std::vector<std::vector<Section>> flow;
    if (columns == 2)
        flow = {{header, rings, cores, engines, apps},
                {[&](float x, float y, float c) { return pool(x, y, c, true); },
                 [&](float x, float y, float c) { return pool(x, y, c, false); }}};
    else
        flow = {{header, rings, cores, engines,
                 [&](float x, float y, float c) { return pool(x, y, c, true); },
                 [&](float x, float y, float c) { return pool(x, y, c, false); }, apps}};
    float bottom = 0;
    for (size_t c = 0; c < flow.size(); ++c)
    {
        const float x = pad + float(c) * (cw + columnGap);
        float y = pad;
        for (size_t k = 0; k < flow[c].size(); ++k)
            y += flow[c][k](x, y, cw) + (k + 1 < flow[c].size() ? sectionGap : 0);
        bottom = std::max(bottom, y);
    }
    // The footer sits on the bottom edge whenever the panel is taller than
    // its content.
    const float footer = std::max(bottom + sectionGap, h - 20 - 15);
    const float fx = columns == 2 ? pad + cw + columnGap : pad;
    text(range == Range::Minutes ? L"one-minute averages" : L"updated every second", fx, footer + 12, cw / 2,
         11, p.muted);
    if (bus)
        text(L"* from the NVIDIA driver", fx + cw / 2, footer + 12, cw / 2, 11, p.muted, regular,
             Align::Right);
    return bottom + sectionGap + 15 + 20;
}
std::wstring Renderer::accessibleText(const Snapshot &s, bool strip, Range range) const
{
    std::wstring text = std::wstring(s.paused ? L"Paused. " : L"") + L"CPU " + formatPercent(s.current[0]) +
                        L"; GPU " + formatPercent(s.current[1]) + L"; dedicated VRAM " +
                        formatBytes(s.vramUsed) + L"; RAM " + formatBytes(s.ramUsed) + L".";
    if (!strip)
    {
        text += std::wstring(L"\nGraphs show the last ") +
                (range == Range::Minutes ? L"60 minutes as one-minute averages." : L"60 seconds.");
        text += L"\n" + s.cpuName + L"; " + std::to_wstring(s.cores.size()) + L" cores; sampled " +
                std::to_wstring(s.updatedMs) + L".";
        for (auto &c : s.cores)
            text += L"\nCore " + std::to_wstring(c.id) + L" " + c.kind + L", " +
                    formatPercent(c.current, true) + L"; history samples " +
                    std::to_wstring(c.history.size(range)) + L".";
    }
    if (!strip)
        for (auto &a : s.apps)
            text += L"\n" + a.name + L", " + std::to_wstring(a.count) + L" processes, CPU " +
                    formatPercent(a.cpu, true) + L", GPU " + formatPercent(a.gpu, true) + L", VRAM " +
                    formatBytes(a.vram) + L", RAM " + formatBytes(a.ram) + L".";
    return text;
}
Snapshot demonstrationSnapshot()
{
    Snapshot s;
    s.updatedMs = GetTickCount64();
    s.gpuName = L"NVIDIA GeForce RTX 5070 Ti";
    s.cpuName = L"Intel Core Ultra 5 245KF";
    // A full hour is generated so that both the one-second and the one-minute
    // range have something to draw in previews and screenshots. The final
    // sixty seconds carry the detailed shape the live view shows.
    const int64_t now = int64_t(s.updatedMs / 1000), hour = 3600;
    for (unsigned id : {0U, 1U, 10U, 11U, 12U, 13U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U})
    {
        CpuCore c;
        c.id = id;
        c.kind = (id < 2 || id >= 10) ? L"Performance" : L"Efficiency";
        c.efficiencyClass = c.kind == L"Performance" ? 1 : 0;
        c.logical = {{0, id}};
        for (int64_t j = 0; j < hour; ++j)
        {
            const double minutes = double(j) / 60, detail = double(j - (hour - 60));
            double v = id == 3   ? 75 + 9 * sin(detail * .16) - 14 * cos(minutes * .21)
                       : id == 8 ? 60 + 8 * sin(detail * .13) + 11 * sin(minutes * .17)
                                 : 4 + 2 * sin(detail * .4 + id) + 3 * (1 + sin(minutes * .3 + id));
            c.current = v;
            c.history.push(now - (hour - 1) + j, {v, missing, missing, missing});
        }
        s.cores.push_back(std::move(c));
    }
    s.ramTotal = 34030211072.;
    s.ramUsed = 12.8 * 1073741824;
    s.vramTotal = 16772022272.;
    s.vramUsed = 3.4 * 1073741824;
    for (int64_t j = 0; j < hour; ++j)
    {
        // `i` walks the last minute exactly as before so the one-second view is
        // unchanged; `m` adds slower movement that only the hour view resolves.
        const double i = double(j - (hour - 60)), m = double(j) / 60;
        Metrics v{12 + 4 * sin(i * .61) + 2 * cos(i * 1.73) + 29 * exp(-pow((i - 19) / 2.8, 2)) +
                      39 * exp(-pow((i - 42) / 1.9, 2)) + 13 * (1 + sin(m * .27)),
                  18 + 6 * sin(i * .23) + 5 * cos(i * .76) + 47 * exp(-pow((i - 31) / 4.2, 2)) +
                      25 * exp(-pow((i - 47) / 2.3, 2)) + 9 * (1 + cos(m * .19)),
                  25. + 7 * (i > 14) + 10 * (i > 30) + 1.3 * sin(i * .2) + 6 * sin(m * .11),
                  35. + 3 * (i > 21) + 1.5 * (i > 38) + .4 * sin(i * .24) + 4 * sin(m * .08)};
        for (auto &n : v)
            n = std::clamp(n, 0., 100.);
        if (j == hour - 1)
            v = {14, 28, 42.5, 40};
        s.history.push(now - (hour - 1) + j, v);
        s.current = v;
    }
    s.apps = {{L"blender", L"Blender", 2, 7.2, 22, 2.1 * 1073741824, 2.4 * 1073741824},
              {L"edge", L"Edge", 12, 2.4, 4.1, 480. * 1048576, 1.8 * 1073741824},
              {L"code", L"VS Code", 8, 1.8, .6, 140. * 1048576, 1.3 * 1073741824},
              {L"discord", L"Discord", 6, .6, .3, 96. * 1048576, 420. * 1048576},
              {L"spotify", L"Spotify", 5, .3, .2, 64. * 1048576, 280. * 1048576}};
    // GPU engines, the memory bus and who holds memory, over the same hour.
    for (const wchar_t *type : {L"3D", L"Copy", L"VideoEncode", L"VideoDecode", L"OFA"})
    {
        auto label = engineLabel(type);
        GpuEngine e;
        e.type = type;
        e.order = label->first;
        e.label = label->second;
        s.engines.push_back(std::move(e));
    }
    s.memoryBusAvailable = true;
    const double gib = 1073741824.0;
    const wchar_t *keys[] = {L"blender", L"edge", L"code", L"discord", L"spotify"};
    for (int64_t j = 0; j < hour; ++j)
    {
        const double i = double(j - (hour - 60)), m = double(j) / 60;
        auto bump = [&](double c, double width) { return exp(-pow((i - c) / width, 2)); };
        auto rise = [&](double at) { return 1 / (1 + exp(-at)); };
        const int64_t second = now - (hour - 1) + j;
        const double d3 = std::clamp(18 + 6 * sin(i * .23) + 47 * bump(31, 4.2) + 25 * bump(47, 2.3) +
                                         9 * (1 + cos(m * .19)),
                                     0., 100.);
        const double engine[] = {d3, std::clamp(3 + 26 * bump(29, 1.5) + 14 * bump(46, 1.2), 0., 100.),
                                 i < 20 ? 1. : 21 + 3 * sin(i * .5), 8 + 2 * sin(i * .3),
                                 std::clamp(44 * bump(31, 3.2) + 20 * bump(47, 1.8), 0., 100.)};
        for (size_t k = 0; k < s.engines.size(); ++k)
        {
            s.engines[k].current = engine[k];
            s.engines[k].history.push(second, {engine[k], missing, missing, missing});
        }
        s.paging = (2 + 70 * bump(21, 1.4) + 18 * bump(23, 1.2)) / 100 * 64 * 1048576;
        s.memoryBus = std::clamp(10 + .6 * d3, 0., 100.);
        s.memoryMetrics.push(second, {s.paging, s.memoryBus, missing, missing});
        const double owners[][2] = {
            {2.2 + .4 * rise(i - 31) + .2 * sin(m * .21), 1.1 + 1.15 * rise(.9 * i - 27)},
            {1.78 + .13 * rise(i - 12), .5},
            {1.4, .15},
            {.42, .1},
            {.28, .07}};
        MemorySample sample;
        double held = 0, heldVram = 0;
        for (int k = 0; k < 5; ++k)
        {
            sample.owners[sample.count++] = {ownerId(keys[k]), float(owners[k][0] * gib),
                                             float(owners[k][1] * gib)};
            held += owners[k][0];
            heldVram += owners[k][1];
        }
        sample.inUse = float((held + 6.6 + .35 * rise(i - 21)) * gib);
        sample.cache = float((13.4 - .3 * rise(i - 21) - .4 * rise(i - 31) + .5 * sin(m * .13)) * gib);
        sample.free = float(s.ramTotal - sample.inUse - sample.cache);
        sample.vramUsed = float((heldVram + .72) * gib);
        s.memory.push(second, sample);
        s.ramCache = sample.cache;
        s.ramFree = sample.free;
    }
    return s;
}
} // namespace perf
