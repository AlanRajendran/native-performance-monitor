#include "render.h"
#include <algorithm>
#include <cwchar>
#include <wincodec.h>

namespace perf
{
using Microsoft::WRL::ComPtr;
static D2D1_COLOR_F color(UINT32 rgb, float a = 1)
{
    return D2D1::ColorF(rgb, a);
}
Palette palette(bool dark, bool high)
{
    Palette p =
        dark ? Palette{color(0x252930), color(0xf3f4f7),
                       color(0xb2bac6), color(0x424952),
                       color(0x3a414a), {color(0x65bded), color(0xba9be7), color(0x62cbd1), color(0xeab561)}}
             : Palette{color(0xf6f7f9), color(0x1c1e22),
                       color(0x5c626b), color(0xd3d8df),
                       color(0xe0e4e9), {color(0x0077b5), color(0x8056b7), color(0x007e83), color(0xb66e13)}};
    p.surface.a = .62f;
    p.border.a = .40f;
    p.grid.a = .40f;
    if (high)
    {
        auto cv = [](int id)
        {
            auto c = GetSysColor(id);
            return D2D1::ColorF(float(GetRValue(c)) / 255, float(GetGValue(c)) / 255,
                                float(GetBValue(c)) / 255);
        };
        p.surface = cv(COLOR_WINDOW);
        p.text = p.muted = cv(COLOR_WINDOWTEXT);
        p.border = p.grid = p.text;
        for (auto &c : p.series)
            c = p.text;
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
    return SUCCEEDED(frame->WriteSource(converter.Get(), nullptr)) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
}
bool Renderer::initialize()
{
    return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf())) &&
           SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown **>(textFactory_.GetAddressOf())));
}
IDWriteTextFormat *Renderer::format(float size, bool strong)
{
    auto key = std::pair{int(size * 10), strong};
    auto &f = formats_[key];
    if (!f)
    {
        textFactory_->CreateTextFormat(L"Segoe UI", nullptr,
                                       strong ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"", &f);
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
                             bool locked, float scroll)
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
    hr = drawTarget(dcTarget_.Get(), b.width * 96.f / dpi, b.height * 96.f / dpi, s, p, strip, locked, false,
                    scroll);
    if (hr == D2DERR_RECREATE_TARGET)
        dcTarget_.Reset();
    return hr;
}
HRESULT Renderer::drawTarget(ID2D1RenderTarget *t, float w, float h, const Snapshot &s, const Palette &p,
                             bool isStrip, bool locked, bool mica, float scroll)
{
    ComPtr<ID2D1SolidColorBrush> brush;
    auto hr = t->CreateSolidColorBrush(p.text, &brush);
    if (FAILED(hr))
        return hr;
    auto set = [&](D2D1_COLOR_F c) { brush->SetColor(c); };
    auto line = [&](float x, float y, float x2, float y2, D2D1_COLOR_F c, float width = 1.f)
    {
        set(c);
        t->DrawLine({x, y}, {x2, y2}, brush.Get(), width);
    };
    auto text = [&](const std::wstring &v, float x, float y, float width, float size, D2D1_COLOR_F c,
                    bool strong = false, bool right = false)
    {
        auto f = format(size, strong);
        if (!f)
            return;
        f->SetTextAlignment(right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        set(c);
        t->DrawTextW(v.c_str(), UINT32(v.size()), f, D2D1::RectF(x, y, x + width, y + size * 1.7f),
                     brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    auto ordered = s.history.ordered();
    auto graph = [&](float x, float y, float width, float height, int metric, bool mini)
    {
        if (!mini)
        {
            for (int j = 1; j < 6; ++j)
                line(x + width * j / 6, y, x + width * j / 6, y + height, p.grid, .5f);
            for (int j = 1; j < 4; ++j)
                line(x, y + height * j / 4, x + width, y + height * j / 4, p.grid, .5f);
        }
        for (int start = 0; start < 60;)
        {
            while (start < 60 && !valid(ordered[start].values[metric]))
                ++start;
            if (start == 60)
                break;
            int end = start;
            while (end + 1 < 60 && valid(ordered[end + 1].values[metric]))
                ++end;
            auto point = [&](int i)
            {
                return D2D1::Point2F(
                    x + width * i / 59,
                    y + height * (1 - float(std::clamp(ordered[i].values[metric], 0.0, 100.0)) / 100));
            };
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink)))
            {
                auto a = point(start);
                sink->BeginFigure({a.x, y + height}, D2D1_FIGURE_BEGIN_FILLED);
                for (int i = start; i <= end; ++i)
                    sink->AddLine(point(i));
                sink->AddLine({point(end).x, y + height});
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                auto fill = p.series[metric];
                fill.a = p.highContrast ? .08f : .13f;
                set(fill);
                t->FillGeometry(path.Get(), brush.Get());
            }
            for (int i = start + 1; i <= end; ++i)
            {
                auto a = point(i - 1), b = point(i);
                line(a.x, a.y, b.x, b.y, p.series[metric], 1.2f);
            }
            if (start == end)
            {
                set(p.series[metric]);
                auto a = point(start);
                t->FillEllipse(D2D1::Ellipse(a, 1, 1), brush.Get());
            }
            start = end + 1;
        }
        if (!mini)
        {
            set(p.border);
            t->DrawRectangle(D2D1::RectF(x, y, x + width, y + height), brush.Get(), .6f);
        }
    };
    t->BeginDraw();
    t->SetTransform(D2D1::Matrix3x2F::Identity());
    t->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    t->Clear(D2D1::ColorF(0, 0));
    auto bg = p.surface;
    if (mica)
        bg.a = .35f;
    set(bg);
    t->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), isStrip ? 7.f : 9.f, isStrip ? 7.f : 9.f), brush.Get());
    set(p.border);
    t->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(.5f, .5f, w - .5f, h - .5f), isStrip ? 7.f : 9.f, isStrip ? 7.f : 9.f),
        brush.Get(), .65f);
    const bool stale = !s.paused && s.updatedMs && GetTickCount64() > s.updatedMs + 4000;
    if (isStrip)
    {
        static const wchar_t *names[] = {L"CPU", L"GPU", L"VRAM", L"RAM"};
        for (int i = 0; i < 4; ++i)
        {
            float x = i * w / 4, cell = w / 4;
            if (i)
                line(x, 10, x, h - 10, p.border, .65f);
            text(names[i], x + 9, 4, cell - 18, 10, p.muted);
            auto value = s.paused ? L"Ⅱ" : stale ? L"—" : formatPercent(s.current[i]);
            text(value, x + 9, 4, cell - 18, 10, p.text, false, true);
            graph(x + 9, h < 48 ? 21.f : 25.f, cell - 18, h - (h < 48 ? 26.f : 33.f), i, true);
        }
    }
    else
    {
        t->PushAxisAlignedClip(D2D1::RectF(0, 0, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        t->SetTransform(D2D1::Matrix3x2F::Translation(0, -scroll));
        const bool compact = w < 400 || h < 520;
        float margin = compact ? 12.f : 16.f, row = compact ? 52.f : 68.f, gy = compact ? 54.f : 62.f,
              gh = compact ? 38.f : 52.f;
        float graphW = w - margin * 2 - 150, metricX = margin + graphW + 18, metricW = w - metricX - margin;
        text(L"Performance", margin, 13, w - 130, 16, p.text, true);
        text(s.paused ? L"Paused"
             : stale  ? L"Stale"
             : locked ? L"Locked"
                      : L"Move / resize",
             w - 132, 18, 116, 11, p.muted, false, true);
        static const wchar_t *names[] = {L"CPU", L"GPU", L"VRAM", L"RAM"};
        for (int i = 0; i < 4; ++i)
        {
            float yy = gy + row * i;
            graph(margin, yy, graphW, gh, i, false);
            text(names[i], metricX, yy - 3, metricW, 12, p.text, true);
            std::wstring value, caption;
            if (i < 2)
            {
                value = formatPercent(s.current[i]);
                caption = i == 0 ? L"All processors" : s.gpuName;
            }
            else
            {
                double used = i == 2 ? s.vramUsed : s.ramUsed, total = i == 2 ? s.vramTotal : s.ramTotal;
                if (i == 2 && total == 0)
                {
                    value = L"N/A";
                    caption = L"Shared GPU";
                }
                else
                {
                    if (valid(used) && valid(total))
                    {
                        wchar_t b[80];
                        swprintf_s(b, L"%.1f / %.0f GiB", used / 1073741824, total / 1073741824);
                        value = b;
                    }
                    else
                        value = L"—";
                    caption = (i == 2 ? L"Dedicated · " : L"Physical · ") + formatPercent(s.current[i]);
                }
            }
            text(value, metricX, yy + (compact ? 11.f : 14.f), metricW,
                 compact ? 16.f : (i < 2 ? 22.f : 17.f), p.text);
            if (!compact)
                text(caption, metricX, yy + 38, metricW, 10.5f, p.muted);
        }
        float footer = gy + row * 3 + gh + 5;
        text(L"60 seconds", margin, footer, 100, 10.5f, p.muted);
        text(L"Now", margin + graphW - 40, footer, 40, 10.5f, p.muted, false, true);
        text(L"Scale: 0–100%", w - 142, footer, 126, 10.5f, p.muted, false, true);
        float divider = footer + 25, title = divider + 11, head = title + 27, first = head + 25,
              rowH = compact ? 23.f : 26.f;
        line(margin, divider, w - margin, divider, p.border, .65f);
        text(L"Top applications", margin, title, 180, 13, p.text, true);
        text(L"Resource pressure", w - 153, title + 2, 137, 10.5f, p.muted, false, true);
        float end = w - margin;
        std::array<float, 4> ends{end - (compact ? 165.f : 185.f), end - (compact ? 120.f : 132.f),
                                  end - (compact ? 60.f : 65.f), end};
        text(L"Application", margin, head, ends[0] - margin - 43, 10.5f, p.muted);
        for (int i = 0; i < 4; ++i)
            text(names[i], ends[i] - (i < 2 ? 43.f : 60.f), head, i < 2 ? 43.f : 60.f, 10.5f, p.muted, false,
                 true);
        for (size_t i = 0; i < s.apps.size() && i < 5; ++i)
        {
            auto &a = s.apps[i];
            float yy = first + rowH * float(i);
            float nameW = ends[0] - margin - 50;
            std::wstring label = a.name + (a.count > 1 ? L" (" + std::to_wstring(a.count) + L")" : L"");
            text(label, margin, yy, nameW, 11, p.text);
            std::array<std::wstring, 4> values{formatPercent(a.cpu, true), formatPercent(a.gpu, true),
                                               formatBytes(a.vram, compact), formatBytes(a.ram, compact)};
            for (int j = 0; j < 4; ++j)
                text(values[j], ends[j] - (j < 2 ? 46.f : 63.f), yy, j < 2 ? 46.f : 63.f, 11, p.text, false,
                     true);
        }
        if (s.apps.empty())
            text(s.updatedMs ? L"Waiting for application counters…" : L"Starting counters…", margin, first,
                 w - margin * 2, 12, p.muted);
        if (h < (compact ? 460 : 548))
            text(L"Unlock to scroll", w - 133, std::max(h, 460.f) - 23, 117, 10.5f, p.muted, false, true);
        t->SetTransform(D2D1::Matrix3x2F::Identity());
        t->PopAxisAlignedClip();
    }
    return t->EndDraw();
}
std::wstring Renderer::accessibleText(const Snapshot &s, bool strip) const
{
    std::wstring text = std::wstring(s.paused ? L"Paused. " : L"") + L"CPU " + formatPercent(s.current[0]) +
                        L"; GPU " + formatPercent(s.current[1]) + L"; dedicated VRAM " +
                        formatBytes(s.vramUsed) + L"; RAM " + formatBytes(s.ramUsed) + L".";
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
    s.gpuName = L"GPU 0 · Discrete";
    s.ramTotal = 32. * 1073741824;
    s.ramUsed = 12.8 * 1073741824;
    s.vramTotal = 8. * 1073741824;
    s.vramUsed = 3.4 * 1073741824;
    for (int i = 0; i < 60; ++i)
    {
        Metrics v{12 + 4 * sin(i * .61) + 2 * cos(i * 1.73) + 29 * exp(-pow((i - 19) / 2.8, 2)) +
                      39 * exp(-pow((i - 42) / 1.9, 2)),
                  18 + 6 * sin(i * .23) + 5 * cos(i * .76) + 47 * exp(-pow((i - 31) / 4.2, 2)) +
                      25 * exp(-pow((i - 47) / 2.3, 2)),
                  25. + 7 * (i > 14) + 10 * (i > 30) + 1.2 * sin(i * .2),
                  35. + 3 * (i > 21) + 1.5 * (i > 38) + .4 * sin(i * .24)};
        if (i == 59)
            v = {14, 28, 42.5, 40};
        s.history.push(int64_t(s.updatedMs / 1000) - 59 + i, v);
        s.current = v;
    }
    s.apps = {{L"blender", L"Blender", 2, 7.2, 22, 2.1 * 1073741824, 2.4 * 1073741824},
              {L"edge", L"Edge", 12, 2.4, 4.1, 480. * 1048576, 1.8 * 1073741824},
              {L"code", L"VS Code", 8, 1.8, .6, 140. * 1048576, 1.2 * 1073741824},
              {L"discord", L"Discord", 6, .6, .3, 96. * 1048576, 420. * 1048576},
              {L"spotify", L"Spotify", 5, .3, .2, 64. * 1048576, 280. * 1048576}};
    return s;
}
} // namespace perf
