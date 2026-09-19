# Changelog

Every release is a git tag (`v2.0.0`, `v1.6.0`, …). Changes are labelled
**Added**, **Changed**, **Fixed** and **Removed**. Each release line installs
side by side with the others and imports the previous line's settings on first
run.

| Version | In one line |
|---|---|
| [2.0.0](#200) | GPU engines, RAM and VRAM by application, one aligned layout, lighter to run |
| [1.6.0](#160) | Ring gauges, per-core heat map, accent colour from Windows |
| [1.5.0](#150) | Panel and strip stay put: taskbar-owned strip, protected desktop panel |
| [1.4.0](#140) | Flicker fixes, 60-minute history, strip embedded in the taskbar |
| [1.3.0](#130) | Installer, Installed Apps entry, opacity slider |
| [1.2.0](#120) | One large panel with every physical core |
| [1.1.0](#110) | Strip inside the taskbar |
| [1.0.0](#100) | First release |

## 2.0.0

The redesign, developed as 1.7. Measured side by side with 1.6 for two minutes
with the panel and strip showing: 0.146% of total CPU against 0.160%, 46 MB
of private memory against 43 MB, despite the larger panel and the new data.

### Added

- **GPU engines over time.** A heat row for each kind of work the graphics card
  does, as Windows reports it: 3D, Copy, Video encode, Video decode, Optical
  flow (frame generation) and JPEG decode. Windows and the drivers do not expose
  individual shader cores; engines are the honest detail.
- **RAM and VRAM by application.** A bar for every sample, split between the
  three busiest apps, Windows and other apps, cache and free space; below it the
  same split right now as a grid of blocks, with each holder's size and its
  change over the range.
- **Paging**, a heat row under RAM: how much Windows reads back from disk, the
  real sign that memory is short.
- **NVIDIA memory bus** (tray menu, off by default): how busy the card's memory
  is, from the NVIDIA management library that ships with the driver. Loading it
  costs about 20 MB, which is why it is opt-in.
- **Two-column layout** when the panel is wider than 860 or the screen is too
  short for one column.
- A timing of one panel frame in the native tests, so releases can be compared.

### Changed

- **One grid.** Every section shares the same columns, so a moment in time
  lines up from the cores down through the memory bars. Four text sizes only.
- The three named apps have their own colours (cornflower, ochre, sage), the
  same in the bars, the blocks and their Working hardest tiles.
- **Lighter drawing.** Everything that rarely changes is drawn once into a
  cached layer; each second only readings, gauge arcs and heat images are drawn
  over it. Heat maps, bars and block grids are written straight into one image
  per section, and text layouts are reused. A panel frame went from 5.9 ms to
  3.7 ms.
- Nothing is drawn while a full-screen app covers the desktop.
- The default panel is 450 × 1340; sizes from earlier versions are reset once,
  since the layout changed.
- Repository: build outputs, old screenshots and benchmark files are no longer
  tracked; the README has a figure of the interface.

## 1.6.0

### Added

- **Ring gauges** for CPU, GPU and memory, with the processor, graphics card or
  memory size underneath and a heat strip of the selected range.
- **Per-core heat map**: one row per physical core, one cell per sample,
  performance cores then efficiency cores.
- **Accent colour from Windows** for peaks and highlights; it follows the
  wallpaper when the accent is set to Automatic, and updates immediately.
- **Working hardest**: the four busiest apps with CPU, GPU, VRAM and RAM side
  by side, the largest share of its capacity highlighted.

### Changed

- Taskbar strip: fixed-width readings, one bar per core for CPU and a
  twelve-step meter for GPU, VRAM and RAM.
- The default panel is 450 × 760.

### Removed

- The per-core line graphs and the application table they replaced.

## 1.5.0

Every change here was grounded in measurements taken with a z-order recorder
on Windows 11 25H2; see [docs/placement.md](docs/placement.md).

### Added

- **Record placement trace** in the tray menu (and `--trace`), writing every
  placement action and its reason to `trace.log`.
- Settings from the previous version are imported on first run, read-only.

### Changed

- The strip is owned by the taskbar and runs on its own thread, which does
  nothing but place and draw it, so nothing in the program can hold up the
  taskbar.
- The system-wide event hook listens only to foreground and minimize events.

### Fixed

- The strip no longer blinks when the taskbar is clicked (246 ms per click in
  1.4, measured 0 ms).
- Show Desktop no longer loses the panel: it becomes temporarily topmost at the
  bottom of the topmost band, then drops back into the desktop layer.
- Nothing can push the locked panel around; it refuses z-order changes it did
  not make.
- The strip recovers from Show Desktop and no longer hides for shell menus.
- The strip waits for its taskbar slot at launch instead of jumping into it.

## 1.4.0

### Added

- **History** in the tray menu: every graph shows 60 seconds or 60 minutes
  (one-minute averages, newest point live).
- Documentation of the architecture, placement, rendering and troubleshooting.

### Changed

- Opacity applies to the panel background only; text and graphs stay opaque.
- The strip looks embedded in the taskbar: no plate, border, corners or
  dividers inside the bar.
- Installs alongside 1.3 as its own release line.
- The taskbar layout is read only while the strip is being fitted, and far
  less often.

### Fixed

- Flicker of the panel and strip from placement being reapplied every second.
- The strip hopping between taskbar slots on ordinary taskbar activity.
- Accessibility text rewritten, and broadcast, every second.
- Surfaces left buried behind the wallpaper host or the taskbar.

## 1.3.0

### Added

- Per-user Setup.exe with desktop and Start menu shortcuts and a Windows
  Installed Apps entry.
- A 10–100% panel opacity slider with live updates.
- Installation, removal, slider and persistence tests.

### Changed

- The taskbar strip is back, on by default, with a fallback above the taskbar.
- Uninstall removes the shortcuts and the Installed Apps entry it created.

### Fixed

- Recovering the locked panel after it is hidden or minimized.
- Turning off the version-specific startup entry.

## 1.2.0

### Added

- One large desktop panel with every physical core and its efficiency class,
  each with its own 60-second history.
- A native Uninstall.exe in the portable folder.

### Changed

- Sampling continues while the panel is covered or hidden.
- Memory is shown in decimal GB and MB; default background opacity is 25%.
- Settings and startup entry are isolated from 1.1.

## 1.1.0

### Added

- The four-graph strip sits in unused taskbar space by default, found with
  read-only, cached UI Automation on a background thread.
- Tests for taskbar gap selection and premultiplied alpha.

### Changed

- Translucent surfaces in both lock states, click-through while locked.

### Fixed

- PNG exports convert premultiplied pixels to straight alpha correctly.

## 1.0.0

### Added

- First native Windows x64 release: one-second graphs, grouped process
  ranking every two seconds, optional per-user startup, portable package and
  guarded uninstall.
