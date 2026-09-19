# Native Performance Monitor 1.6.0

A lightweight native Windows 11 x64 performance monitor with ring gauges, a per-core heat map, a desktop panel and an optional taskbar strip. One monitoring process uses Win32, Direct2D/DirectWrite, DWM, DXGI and user-mode Windows performance counters. No service, browser engine, kernel driver or Explorer injection.

## Install or run portably

Extract the full ZIP into a new folder. **Setup.exe**, **PerfMonitor.exe** and **Uninstall.exe** are visible directly in that folder.

Open **Setup.exe** for normal Windows integration. It installs to `%LOCALAPPDATA%\Programs\NativePerfMonitor-1.6`, creates desktop and Start menu shortcuts, and registers **Native Performance Monitor 1.6** in Windows Settings > Apps > Installed apps. It installs alongside earlier versions rather than replacing them and brings their settings across on first run; remove the earlier version from Windows Installed apps once 1.6 is set up, so its shortcuts cannot launch the old build. Installation is per user and needs no administrator privileges. The optional sign-in startup checkbox is off by default.

For portable use, open **PerfMonitor.exe** directly. Keep all seven files together. Portable operation creates no shortcuts or Installed Apps registration. Exit an older running monitor before launching this version. Earlier versions remain unchanged. This is an unsigned local build with a static C++ runtime.

## Controls and appearance

Right-click the notification-area icon, which may be in the overflow menu. Choose **Panel opacity…** for a continuous native slider from **10% to 100% (opaque)**. Changes apply live and persist; arrow keys adjust one percent.

Opacity applies to the panel background only. Text, gauges, heat cells and meters are always fully opaque, so content stays readable at any setting and over any wallpaper. The taskbar strip keeps its own fixed readable background. Both surfaces follow Windows light/dark settings; high contrast uses solid system colors.

**History** switches every graph between **60 seconds** and **60 minutes**. The hour view shows one-minute averages and covers both the panel and the strip. Both ranges are recorded continuously, so switching is instant and nothing is lost either way; after a fresh start the hour view fills in over the first hour.

The desktop panel starts locked and click-through, sitting in the desktop layer: above the wallpaper, below every application window, like a desktop widget. While locked it refuses to be brought forward or pushed back by anything, which is what keeps it stable. During Show Desktop (Win+D or the far right of the taskbar) it stays visible, and it returns to the desktop layer when your windows come back. Unlock it to move, resize or scroll it like an ordinary window. Explicitly hiding it in the tray menu is respected. Sampling continues while covered or hidden. Pause and Exit remain explicit controls.

**Taskbar strip** is on by default. **Prefer inside taskbar** uses reliable free space between existing controls; otherwise the strip appears immediately above the taskbar. Inside the taskbar the strip is owned by the taskbar window, so clicking the taskbar never covers it, and it is drawn without a background so it reads as part of the bar. It stays in the slot it occupies and only moves when that slot is genuinely taken. Full-screen applications and an auto-hidden bar temporarily hide it. The strip is a separate window, not an Explorer extension; turning **Prefer inside taskbar** off stops the monitor reading the taskbar layout at all.

## Readings

CPU total and physical cores, GPU, dedicated VRAM and RAM are sampled once per second and kept for both 60 seconds and 60 minutes. Five grouped application rows refresh every two seconds. Core labels follow Windows efficiency classes: P/E for two classes, generic cores for homogeneous CPUs, explicit class numbers for more classes. SMT siblings are averaged only when all have valid readings.

CPU is busy time calculated from valid idle counters and may differ from Task Manager frequency-adjusted utilization. GPU is the busiest engine on the selected adapter; dedicated VRAM excludes shared system memory. **GB = 1,000,000,000 bytes** and **MB = 1,000,000 bytes**, calculated from byte counters. Missing values remain dashes and history gaps.

Applications are grouped by normalized executable path; base Windows processes are excluded from ranking but included in overall graphs. Ranking uses the largest CPU, GPU, dedicated-memory or physical-memory share. Table RAM is private resident working set. Process GPU allocations may overlap, so rows need not sum to adapter usage. Linked GPU nodes beyond node zero are unsupported.

## Settings and removal

Settings are isolated in `%LOCALAPPDATA%\NativePerfMonitor-1.6`. Optional startup uses the current user's `NativePerfMonitor-1.6` Run value. On first run 1.6 imports the most recent earlier version's settings, reading them without changing them. Installation does not change system security, theme or taskbar settings.

Remove the installed copy through Windows Installed apps or its **Uninstall.exe**. The native launcher confirms removal and briefly starts an embedded cleanup script through built-in Windows PowerShell so both executables can be deleted. Cleanup checks identity, executable metadata, paths and ownership; it removes exact owned files, matching shortcuts, the matching Installed Apps entry and optional startup registration. Unrelated files and earlier versions remain. Removing a portable copy does not delete another registered installation's settings. No helper stays running.

## Build and validation

Requires Visual Studio 2022 Desktop development with C++, a Windows 11 SDK and CMake 3.24+. No third-party monitoring package is needed.

```powershell
.\scripts\Build.ps1
.\scripts\CreatePackage.ps1
```

Or directly with CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Tests cover calculations, topology, units, premultiplied rendering at 100/125/150/200% DPI, native window behavior, live opacity slider values and keyboard steps, taskbar visibility, background recovery, shortcuts, installed-app registration, startup regression and guarded removal. Window tests require an interactive Explorer desktop and report a skip if unavailable. Installation tests use disposable folders and isolated registry locations; no normal startup entry is modified.

Window tests refuse to run while a monitor instance is live, so they cannot interrupt a copy you are using; exit it before a full test run.

## Documentation

| Document | Contents |
| --- | --- |
| [docs/architecture.md](docs/architecture.md) | processes, threads, data flow, what is deliberately not done |
| [docs/placement.md](docs/placement.md) | window placement rules — read this before changing placement |
| [docs/rendering.md](docs/rendering.md) | drawing pipeline, the transparency rule, history ranges |
| [docs/troubleshooting.md](docs/troubleshooting.md) | symptoms and their causes |
| [docs/repository-layout.md](docs/repository-layout.md) | where everything lives and how to build it |
| [docs/history/](docs/history/) | per-version architecture and validation reports |

`docs/history/validation-1.3.0.md` holds measured results for 1.3.0. Historical reports describe their named versions only.

If something misbehaves, choose **Record placement trace** in the tray menu, reproduce the problem and choose it again; `trace.log` in the settings folder records every placement action and why. See [docs/troubleshooting.md](docs/troubleshooting.md).

Developer options: `--trace` records the placement trace from launch; `--data-dir PATH` isolates settings and disables startup changes; `--benchmark 180 --warmup 30 --report PATH` measures then exits; `--render-preview PATH` exports illustrative native graphics. Setup accepts `--install-no-launch` for an explicitly requested unattended per-user installation without enabling startup. Normal users should open Setup.exe interactively.
