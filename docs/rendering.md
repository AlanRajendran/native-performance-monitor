# Rendering and the transparency model

## The surface pipeline

Both surfaces are `WS_EX_LAYERED` pop-up windows drawn the same way:

```
Snapshot + Palette + Range
        │
        ▼
Renderer::drawBitmap()          software Direct2D, via ID2D1DCRenderTarget
        │                       bound to a 32-bit premultiplied DIB section
        ▼
BitmapSurface (DIB)
        │
        ▼
UpdateLayeredWindow(ULW_ALPHA)  DWM composites the per-pixel alpha
```

There is no `WM_PAINT`-driven drawing and no hardware render target. Per-pixel
alpha is what makes a translucent panel possible over an arbitrary wallpaper;
`UpdateLayeredWindow` is the only way to supply it for a window whose content
this program draws itself.

`Renderer::drawTarget()` is separated from `drawBitmap()` so tests can draw to
any render target without a window.

## The transparency rule

**Exactly one mark carries the user's opacity: the rounded background
rectangle. Everything drawn after it is fully opaque.**

```cpp
// in paint()
p.surface.a = s.strip ? .62f : settings.opacity / 100.f;
```

`palette()` returns every other colour at `a = 1`. Subtlety comes from *blending
colours*, never from lowering alpha:

```cpp
p.grid   = mix(p.plot, p.text, .14f);      // a faint but solid grid line
p.border = mix(p.surface, p.text, .30f);
p.fill[i] = mix(p.plot, p.series[i], .18f); // the area under a trace
```

### Why

Before 1.4.0 the grid lines carried `a = .40` and the area fill under each trace
carried `a = .13`, drawn on top of an already translucent surface. Compositing
is `out = src + dst·(1 − src)`, so a grid line over a 25% background reached
0.55 alpha, and over a 10% background only 0.46. The content faded into the
wallpaper exactly as the user turned the background down — the opposite of what
the setting is for. Text and traces were already at `a = 1` and were never the
problem.

### Gauges, heat and the accent colour

Since 1.6 the panel has no line graphs. Current load is a ring gauge on an
opaque `p.plot` track; history is heat: one opaque cell per sample, coloured
from `p.heatBase` (idle) through `p.accent` to `p.glow` (the top 15%). Every
cell is a solid colour, so the transparency rule holds.

`p.accent` comes from `HKCU\...\Explorer\Accent\AccentPalette`, the eight
shades Windows derives from the accent (and so from the wallpaper when the
accent is Automatic). Dark themes use Light2, light themes Dark1, the same
shades Windows uses on those backgrounds. `palette()` reads it on every call
and the panel repaints on `WM_DWMCOLORIZATIONCOLORCHANGED`, so an accent change
shows at once. High contrast replaces all of it with system colours.

The strip draws fixed-width readings (Cascadia Mono, falling back to Consolas),
per-core bars for CPU and twelve-segment meters for the rest; unlit segments
use the opaque `p.off`.

### The strip has two looks

The strip is drawn one of two ways, chosen by `stripEmbedded()`:

**Above the taskbar** it is its own object, so it gets the full treatment: a
62% plate, a drawn border, rounded corners and cell dividers.

**Inside the taskbar** it must read as taskbar content, not as a card resting on
the bar. So it draws no plate, no border, no dividers, and its area fills are
translucent, tinting the bar rather than covering it. Text and traces stay fully
opaque.

The window frame has to agree with this. The strip thread sets
`DWMWA_WINDOW_CORNER_PREFERENCE` to `DWMWCP_DONOTROUND` when embedded, and
`DWMWA_BORDER_COLOR` is turned off on both surfaces. DWM draws its own hairline
border around a window, and on the embedded strip that border alone was enough
to make it look like a floating card even with every drawn outline removed. The
embedded flag travels in each `StripFrame`, so the frame follows the strip in
and out of the taskbar.

### If you add a mark

Give it `a = 1` and get its subtlety from `mix()`. If you find yourself typing
`colour.a = 0.3f`, you are reintroducing the bug.

High contrast bypasses all of this: `palette(dark, true)` replaces every colour
with a system colour at full alpha, and `paint()` skips the opacity override.

## History ranges

Both ranges are exactly `historyPoints` (60) samples, so the graph code does not
know which one it is drawing — only the axis labels change.

```
push(second, values)
   ├──► seconds_ ring        one point per second, 60 seconds
   └──► minute accumulator ──► minutes_ ring   one point per minute, 60 minutes
```

The accumulator sums valid values per metric within the current wall-clock
minute and rewrites the newest minute point every second with the running mean.
So the hour view advances continuously instead of stepping once a minute, and a
metric that is missing for a whole minute stays missing rather than becoming
zero.

Both rings record all the time, whatever the user selected. Switching ranges is
therefore instant and needs no repaint beyond the one it triggers — there is no
"collecting…" state to handle, except in the first hour after launch, where the
minute ring is simply partly empty and right-aligned like any other gap.

Minute points are **means**, not peaks. A 20-second spike inside a quiet minute
shows as a modest bump. If peak tracking is wanted later, the natural shape is a
second accumulator and a fainter overlay trace, not a change to this one.

## Cost

A panel repaint is a full software Direct2D render of the whole surface followed
by a full `UpdateLayeredWindow`. At 200% DPI the default panel is 900×1760 px.
That is not free, which is why:

- `update()` skips the panel repaint when it is fully occluded
- `describe()` only writes window text when the string actually changed
- placement repaints only when the extent changed

Sampling is independent of all of this and continues while the surfaces are
hidden, so history is never lost to a covered window.

## The 1.7 panel

### One grid

Every time-based section (cores, GPU engines, paging, the RAM and VRAM bars)
uses the same three columns: a 68 DIP label, the samples, and a 40 DIP value,
with 8 DIP gutters. Sample *n* therefore sits at the same x in every section,
so a spike can be read straight down the panel. Text uses four sizes only:
28 (gauge readings), 13 (titles), 12 (names and amounts), 11 (everything
secondary), placed by baseline so mixed sizes on one row line up.

`layoutPanel()` both measures and draws. With no target it only returns the
content height, which is what scrolling and "Scroll for more" use, so the
height can never disagree with what is drawn. Panels at least 860 DIP wide
flow into two columns.

### Drawing cost

- **Rasters, not rectangles.** Heat rows, memory bars and block grids are
  written into a pixel buffer at device resolution (`Raster`) and drawn with
  one `DrawBitmap` per section, nearest-neighbour, so cells stay crisp. About
  2,000 cells become a handful of bitmaps.
- **A cached static layer.** `drawTarget` first records the static marks
  (their text, position and colour) into a key without drawing; only when the
  key differs from the last frame is the layer redrawn. Each second the layer
  is copied and `Pass::Dynamic` draws readings, arcs and rasters on top. The
  panel frame dropped from 5.9 ms to 3.7 ms (see the native tests' printed
  timing).
- **Cached text layouts.** `layout()` keeps `IDWriteTextLayout`s keyed by
  text, size, weight, width and alignment; labels are built once. The cache is
  cleared past 600 entries because changing values add new keys.
- **No drawing when nobody can see it:** hidden panels and a full-screen
  foreground app skip the frame entirely.

### Memory holders

`MemoryHistory` keeps one `MemorySample` per second and the latest sample of
each minute: in-use, cache (standby lists) and free RAM, used VRAM, and the
bytes held by up to twelve applications (the three named in Working hardest
first, then the largest holders of RAM and of VRAM). The panel draws the three
named apps in `p.owners`, "Windows, others" as in-use minus those three, then
cache and free. Application RAM is the private working set, so shared pages
count under "Windows, others".
