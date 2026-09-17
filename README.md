# Native Performance Monitor 1.4.0

A lightweight native Windows 11 x64 performance monitor with compact physical-core graphs, a desktop panel and an optional taskbar strip. One monitoring process uses Win32, Direct2D/DirectWrite, DWM, DXGI and user-mode Windows performance counters. No service, browser engine, kernel driver or Explorer injection.

## Install or run portably

Extract the full ZIP into a new folder. **Setup.exe**, **PerfMonitor.exe** and **Uninstall.exe** are visible directly in that folder.

Open **Setup.exe** for normal Windows integration. It installs to `%LOCALAPPDATA%\Programs\NativePerfMonitor-1.4`, creates desktop and Start menu shortcuts, and registers **Native Performance Monitor 1.4** in Windows Settings > Apps > Installed apps. It installs alongside 1.3 rather than replacing it; remove 1.3 from Windows Installed apps once 1.4 is set up. Installation is per user and needs no administrator privileges. The optional sign-in startup checkbox is off by default.

For portable use, open **PerfMonitor.exe** directly. Keep all seven files together. Portable operation creates no shortcuts or Installed Apps registration. Exit an older running monitor before launching this version. Earlier versions remain unchanged. This is an unsigned local build with a static C++ runtime.

## Controls and appearance

Right-click the notification-area icon, which may be in the overflow menu. Choose **Panel opacity…** for a continuous native slider from **10% to 100% (opaque)**. Changes apply live and persist; arrow keys adjust one percent.

Opacity applies to the panel background only. Text, traces, grids and the graph cards are always fully opaque, so content stays readable at any setting and over any wallpaper. The taskbar strip keeps its own fixed readable background. Both surfaces follow Windows light/dark settings; high contrast uses solid system colors.

**History** switches every graph between **60 seconds** and **60 minutes**. The hour view shows one-minute averages and covers both the panel and the strip. Both ranges are recorded continuously, so switching is instant and nothing is lost either way; after a fresh start the hour view fills in over the first hour.

The desktop panel starts locked and click-through, behind normal application windows on the primary monitor. Unlock to move, resize or scroll it. Its position and visibility recover after unexpected shell hiding or minimization, and a once-per-second check repairs incorrect stacking — rate limited, and suspended while a full-screen application is in the foreground so it cannot disturb a game. Explicitly hiding it in the tray menu is respected. Sampling continues while covered or hidden. Pause and Exit remain explicit controls.

**Taskbar strip** is on by default. **Prefer inside taskbar** uses reliable free space between existing controls; otherwise the strip appears immediately above the taskbar instead of disappearing. Once placed, the strip stays in the slot it occupies and only moves when that slot is genuinely taken, so ordinary taskbar activity does not shift it. Full-screen applications, an auto-hidden bar and overlapping shell menus temporarily hide it. The strip is a separate native overlay, not an Explorer extension; turning **Prefer inside taskbar** off stops the monitor reading the taskbar layout at all.

## Readings

CPU total and physical cores, GPU, dedicated VRAM and RAM are sampled once per second and kept for both 60 seconds and 60 minutes. Five grouped application rows refresh every two seconds. Core labels follow Windows efficiency classes: P/E for two classes, generic cores for homogeneous CPUs, explicit class numbers for more classes. SMT siblings are averaged only when all have valid readings.

CPU is busy time calculated from valid idle counters and may differ from Task Manager frequency-adjusted utilization. GPU is the busiest engine on the selected adapter; dedicated VRAM excludes shared system memory. **GB = 1,000,000,000 bytes** and **MB = 1,000,000 bytes**, calculated from byte counters. Missing values remain dashes and history gaps.

Applications are grouped by normalized executable path; base Windows processes are excluded from ranking but included in overall graphs. Ranking uses the largest CPU, GPU, dedicated-memory or physical-memory share. Table RAM is private resident working set. Process GPU allocations may overlap, so rows need not sum to adapter usage. Linked GPU nodes beyond node zero are unsupported.

## Settings and removal

Settings are isolated in `%LOCALAPPDATA%\NativePerfMonitor-1.4`. Optional startup uses the current user's `NativePerfMonitor-1.4` Run value. Each release line keeps its own settings, so 1.4 starts from defaults rather than inheriting 1.3's. Installation does not change system security, theme or taskbar settings.

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

Developer options: `--data-dir PATH` isolates settings and disables startup changes; `--benchmark 180 --warmup 30 --report PATH` measures then exits; `--render-preview PATH` exports illustrative native graphics. Setup accepts `--install-no-launch` for an explicitly requested unattended per-user installation without enabling startup. Normal users should open Setup.exe interactively.
