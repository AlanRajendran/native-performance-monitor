#include "render.h"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
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
        p.heatBase = p.off = p.plot;
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
        dcTarget_.Reset();
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
float Renderer::measure(const std::wstring &v, float size, DWRITE_FONT_WEIGHT weight, bool mono)
{
    ComPtr<IDWriteTextLayout> layout;
    auto f = format(size, weight, mono);
    if (!f || FAILED(textFactory_->CreateTextLayout(v.c_str(), UINT32(v.size()), f, 4096, 256, &layout)))
        return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}
HRESULT Renderer::drawTarget(ID2D1RenderTarget *t, float w, float h, const Snapshot &s, const Palette &p,
                             bool isStrip, bool locked, float scroll, Range range, bool embedded)
{
    ComPtr<ID2D1SolidColorBrush> brush;
    auto hr = t->CreateSolidColorBrush(p.text, &brush);
    if (FAILED(hr))
        return hr;
    constexpr auto regular = DWRITE_FONT_WEIGHT_NORMAL, light = DWRITE_FONT_WEIGHT_LIGHT,
                   strong = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    auto set = [&](D2D1_COLOR_F c) { brush->SetColor(c); };
    auto box = [&](float x, float y, float bw, float bh, D2D1_COLOR_F c, float radius = 0)
    {
        set(c);
        auto r = D2D1::RectF(x, y, x + bw, y + bh);
        if (radius > 0)
            t->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.Get());
        else
            t->FillRectangle(r, brush.Get());
    };
    auto text = [&](const std::wstring &v, float x, float y, float width, float size, D2D1_COLOR_F c,
                    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL, Align align = Align::Left,
                    bool mono = false)
    {
        auto f = format(size, weight, mono);
        if (!f)
            return;
        f->SetTextAlignment(align == Align::Right    ? DWRITE_TEXT_ALIGNMENT_TRAILING
                            : align == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                     : DWRITE_TEXT_ALIGNMENT_LEADING);
        set(c);
        t->DrawTextW(v.c_str(), UINT32(v.size()), f, D2D1::RectF(x, y, x + width, y + size * 1.7f),
                     brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    // A gauge: an opaque track and an arc clockwise from twelve o'clock. Even
    // zero draws a round dot so the gauge never looks broken.
    auto gauge = [&](float cx, float cy, float r, float stroke, double v)
    {
        set(p.plot);
        t->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), brush.Get(), stroke);
        if (!valid(v))
            return;
        const float f = std::max(float(std::min(v, 100.0)) / 100.f, .01f);
        set(p.accent);
        if (f > .999f)
        {
            t->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), brush.Get(), stroke);
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
        t->DrawGeometry(path.Get(), brush.Get(), stroke, roundCap_.Get());
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
    if (!embedded)
    {
        set(p.surface);
        t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), radius, radius), brush.Get());
        set(p.border);
        t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(.5f, .5f, w - .5f, h - .5f), radius, radius),
                                brush.Get(), .65f);
    }
    const bool stale = !s.paused && s.updatedMs && GetTickCount64() > s.updatedMs + 4000,
               live = !s.paused && !stale;
    auto order = coreOrder(s.cores);
    if (isStrip)
    {
        // Instrument strip: fixed-width readings; CPU shows one bar per
        // physical core, the others a twelve-step meter.
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
    t->PushAxisAlignedClip(D2D1::RectF(0, 0, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    t->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll));
    const float m = 20, usable = w - 2 * m;
    text(L"This PC", m, 18, usable / 2, 15, mix(p.text, p.muted, .35f), strong);
    text(s.paused                  ? L"Paused"
         : stale                   ? L"Stale"
         : !locked                 ? L"Unlocked · drag to move"
         : range == Range::Minutes ? L"last hour"
                                   : L"last minute",
         m + usable / 3, 21, usable * 2 / 3, 12, p.muted, regular, Align::Right);

    // Gauges for the three things people glance at, each with the model or
    // capacity underneath and a strip of heat for the selected range.
    const auto history = s.history.ordered(range);
    const float col = (usable - 24) / 3, d = std::min(116.f, col - 8), stroke = d * 9 / 116;
    float y = 60;
    wchar_t memory[64] = L"—";
    if (valid(s.ramUsed) && valid(s.ramTotal) && s.ramTotal > 0)
        swprintf_s(memory, L"%.1f of %.1f GB", s.ramUsed / 1e9, s.ramTotal / 1e9);
    const struct
    {
        const wchar_t *name;
        int metric;
        std::wstring detail;
    } gauges[] = {
        {L"CPU", 0, shortName(s.cpuName)}, {L"GPU", 1, shortName(s.gpuName)}, {L"Memory", 3, memory}};
    for (int g = 0; g < 3; ++g)
    {
        const auto &item = gauges[g];
        const float cx = m + float(g) * (col + 12) + col / 2, cy = y + d / 2;
        const double v = live ? s.current[item.metric] : missing;
        gauge(cx, cy, (d - stroke) / 2, stroke, v);
        const float big = d * 34 / 116, unit = d * 14 / 116, top = cy - .73f * big;
        const std::wstring value = valid(v)   ? std::to_wstring(int(std::lround(std::min(v, 100.0))))
                                   : s.paused ? L"Ⅱ"
                                              : L"—";
        const float vw = measure(value, big, light), pw = valid(v) ? measure(L"%", unit) : 0;
        const float left = cx - (vw + pw) / 2;
        text(value, left, top, vw + 2, big, p.text, light);
        if (pw)
            text(L"%", left + vw, top + 1.079f * (big - unit), pw + 2, unit, p.muted);
        text(item.name, cx - col / 2, y + d + 8, col, 13, p.text, strong, Align::Center);
        text(item.detail.empty() ? L"—" : item.detail, cx - col / 2, y + d + 25, col, 11, p.muted, regular,
             Align::Center);
        const float tw = std::min(112.f, col), cw = (tw - 29) / 30;
        for (int k = 0; k < 30; ++k)
        {
            const double a = history[2 * k].values[item.metric], b = history[2 * k + 1].values[item.metric];
            const double peak = valid(a) && valid(b) ? std::max(a, b) : valid(a) ? a : b;
            if (valid(peak))
                box(cx - tw / 2 + float(k) * (cw + 1), y + d + 48, cw, 6, heat(p, p.heatBase, peak), 1);
        }
    }
    y += d + 76;

    // Cores × time: one row per physical core, one cell per sample, coloured
    // by load. Reads at a glance where the old per-core line graphs did not.
    unsigned performance = 0, efficiency = 0, busy = 0;
    for (auto &c : s.cores)
    {
        performance += c.kind == L"Performance";
        efficiency += c.kind == L"Efficiency";
        busy += live && valid(c.current) && c.current > 30;
    }
    const auto count = [](unsigned n) { return std::to_wstring(n); };
    const std::wstring summary =
        (performance && efficiency ? count(performance) + L" P · " + count(efficiency) + L" E"
                                   : count(unsigned(s.cores.size())) + L" cores") +
        L" · " + count(busy) + L" busy";
    text(L"Cores", m, y, 60, 13, p.text, strong);
    text(summary, m + measure(L"Cores", 13, strong) + 8, y + 2, usable / 2, 11, p.muted);
    {
        float x = w - m - measure(L"peak", 11);
        text(L"peak", x, y + 1, 40, 11, p.muted);
        x -= 4 + 5 * 14;
        for (int k = 0; k < 5; ++k)
            box(x + float(k) * 14, y + 4, 10, 10, heat(p, p.heatBase, k * 25.0), 2);
        const float idle = measure(L"idle", 11);
        text(L"idle", x - 4 - idle, y + 1, idle + 2, 11, p.muted);
    }
    y += 27;
    if (order.empty())
    {
        text(L"Waiting for CPU topology…", m, y, usable, 11, p.muted);
        y += 24;
    }
    else
    {
        const float pitch = order.size() > 24 ? 8.f : 12.f, row = pitch - 2, gx = m + 32,
                    gw = usable - 32 - 36, cw = (gw - float(historyPoints - 1)) / float(historyPoints);
        const bool hybrid = performance && efficiency;
        for (size_t k = 0; k < order.size(); ++k)
        {
            const auto &c = s.cores[order[k]];
            if (k && c.efficiencyClass != s.cores[order[k - 1]].efficiencyClass)
                y += 8;
            if (row >= 10)
            {
                text((hybrid ? std::wstring(1, c.kind[0]) : L"") + std::to_wstring(c.id), m, y - 2.5f, 28, 10,
                     p.muted);
                text(live ? percentText(c.current) : L"—", w - m - 34, y - 2.5f, 34, 10,
                     live && valid(c.current) && c.current > 30 ? p.accent : p.muted, regular, Align::Right);
            }
            const auto values = c.history.ordered(range);
            for (size_t i = 0; i < historyPoints; ++i)
                if (valid(values[i].values[0]))
                    box(gx + float(i) * (cw + 1), y, cw, row, heat(p, p.heatBase, values[i].values[0]), 1.5f);
            y += pitch;
        }
        y -= 2;
    }
    y += 22;

    // Working hardest: the four measurements side by side, with whichever is
    // the largest share of its capacity in the accent.
    const float columnWidth = 50, valuesX = w - m - 4 * columnWidth;
    static const wchar_t *heads[] = {L"CPU", L"GPU", L"VRAM", L"RAM"};
    static const wchar_t *what[] = {L"CPU", L"GPU", L"VRAM", L"memory"};
    text(L"Working hardest", m, y, valuesX - m, 13, p.text, strong);
    for (int k = 0; k < 4; ++k)
        text(heads[k], valuesX + float(k) * columnWidth, y + 3, columnWidth, 10, p.muted, regular,
             Align::Right);
    y += 27;
    for (size_t i = 0; i < s.apps.size() && i < 4; ++i)
    {
        const auto &a = s.apps[i];
        const float ry = y + float(i) * 42;
        const std::array<double, 4> share{
            valid(a.cpu) ? a.cpu / 100 : -1, valid(a.gpu) ? a.gpu / 100 : -1,
            valid(a.vram) && valid(s.vramTotal) && s.vramTotal > 0 ? a.vram / s.vramTotal : -1,
            valid(a.ram) && valid(s.ramTotal) && s.ramTotal > 0 ? a.ram / s.ramTotal : -1};
        const auto top = int(std::max_element(share.begin(), share.end()) - share.begin());
        wchar_t initial = L'?';
        for (auto ch : a.name)
            if (iswalnum(ch))
            {
                initial = towupper(ch);
                break;
            }
        box(m, ry, 32, 32, p.plot, 9);
        text(std::wstring(1, initial), m, ry + 7, 32, 13, mix(p.text, p.muted, .3f), strong, Align::Center);
        const float nx = m + 44, nw = valuesX - 12 - nx;
        text(a.name, nx, ry - 1, nw, 13, p.text);
        std::wstring why = count(a.count) + (a.count == 1 ? L" process" : L" processes");
        if (share[top] >= 0)
            why += std::wstring(L" · mostly ") + what[top];
        text(why, nx, ry + 17, nw, 11, p.muted);
        const std::array<std::wstring, 4> values{percentText(a.cpu), percentText(a.gpu), sizeText(a.vram),
                                                 sizeText(a.ram)};
        for (int k = 0; k < 4; ++k)
            text(values[k], valuesX + float(k) * columnWidth - 10, ry + 6, columnWidth + 10, 14,
                 k == top && share[top] >= 0 ? p.accent : p.text, light, Align::Right);
    }
    if (s.apps.empty())
        text(L"Waiting for application counters…", m, y, usable, 11, p.muted);
    y += 4 * 42 - 10 + 22;

    // The footer sits at the bottom edge whenever the panel is taller than
    // its content, as the design has it.
    const float fy = std::max(y, h - 33);
    std::wstring vram = L"VRAM —";
    if (valid(s.vramUsed) && valid(s.vramTotal) && s.vramTotal > 0)
    {
        wchar_t b[64];
        swprintf_s(b, L"VRAM %.2f of %.2f GB", s.vramUsed / 1e9, s.vramTotal / 1e9);
        vram = b;
    }
    text(vram, m, fy, usable * .6f, 11, p.muted);
    text(range == Range::Minutes ? L"one-minute averages" : L"updated every second", m + usable * .4f, fy,
         usable * .6f, 11, p.muted, regular, Align::Right);
    t->SetTransform(D2D1::Matrix3x2F::Identity());
    t->PopAxisAlignedClip();
    if (panelContentHeight(s.cores) > h && !locked)
        text(L"Scroll for more", w - 125, h - 19, 105, 9, p.muted, regular, Align::Right);
    return t->EndDraw();
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
    return s;
}
} // namespace perf
