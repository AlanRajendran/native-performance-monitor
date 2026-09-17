# Native Performance Monitor 1.3.0

A lightweight native Windows 11 x64 performance monitor with compact physical-core graphs, a desktop panel and an optional taskbar strip. One monitoring process uses Win32, Direct2D/DirectWrite, DWM, DXGI and user-mode Windows performance counters. No service, browser engine, kernel driver or Explorer injection.

## Install or run portably

Extract the full ZIP into a new folder. **Setup.exe**, **PerfMonitor.exe** and **Uninstall.exe** are visible directly in that folder.

Open **Setup.exe** for normal Windows integration. It installs to `%LOCALAPPDATA%\Programs\NativePerfMonitor-1.3`, creates desktop and Start menu shortcuts, and registers **Native Performance Monitor 1.3** in Windows Settings > Apps > Installed apps. Installation is per user and needs no administrator privileges. The optional sign-in startup checkbox is off by default.

For portable use, open **PerfMonitor.exe** directly. Keep all seven files together. Portable operation creates no shortcuts or Installed Apps registration. Exit an older running monitor before launching this version. Earlier versions remain unchanged. This is an unsigned local build with a static C++ runtime.

## Controls and appearance

Right-click the notification-area icon, which may be in the overflow menu. Choose **Panel opacity…** for a continuous native slider from **10% to 100% (opaque)**. Changes apply live and persist; arrow keys adjust one percent. Foreground text and graph lines stay crisp. The taskbar strip has a separate readable translucent background. Both surfaces follow Windows light/dark settings; high contrast uses solid system colors.

The desktop panel starts locked and click-through, behind normal application windows on the primary monitor. Unlock to move, resize or scroll it. Its position and visibility recover after unexpected shell hiding/minimization; a once-per-second check also repairs incorrect stacking. Explicitly hiding it in the tray menu is respected. Sampling continues while covered or hidden. Pause and Exit remain explicit controls.

**Taskbar strip** is on by default. **Prefer inside taskbar** uses reliable free space between existing controls; otherwise the strip appears immediately above the taskbar instead of disappearing. Full-screen applications, an auto-hidden bar and overlapping shell menus may temporarily hide it. The strip is a separate native overlay, not an Explorer extension.

## Readings

CPU total and physical cores, GPU, dedicated VRAM and RAM have 60-second histories sampled once per second. Five grouped application rows refresh every two seconds. Core labels follow Windows efficiency classes: P/E for two classes, generic cores for homogeneous CPUs, explicit class numbers for more classes. SMT siblings are averaged only when all have valid readings.

CPU is busy time calculated from valid idle counters and may differ from Task Manager frequency-adjusted utilization. GPU is the busiest engine on the selected adapter; dedicated VRAM excludes shared system memory. **GB = 1,000,000,000 bytes** and **MB = 1,000,000 bytes**, calculated from byte counters. Missing values remain dashes and history gaps.

Applications are grouped by normalized executable path; base Windows processes are excluded from ranking but included in overall graphs. Ranking uses the largest CPU, GPU, dedicated-memory or physical-memory share. Table RAM is private resident working set. Process GPU allocations may overlap, so rows need not sum to adapter usage. Linked GPU nodes beyond node zero are unsupported.

## Settings and removal

Settings are isolated in `%LOCALAPPDATA%\NativePerfMonitor-1.3`. Optional startup uses the current user's `NativePerfMonitor-1.3` Run value. Disabling startup now deletes the correct value. Installation does not change system security, theme or taskbar settings.

Remove the installed copy through Windows Installed apps or its **Uninstall.exe**. The native launcher confirms removal and briefly starts an embedded cleanup script through built-in Windows PowerShell so both executables can be deleted. Cleanup checks identity, executable metadata, paths and ownership; it removes exact owned files, matching shortcuts, the matching Installed Apps entry and optional startup registration. Unrelated files and earlier versions remain. Removing a portable copy does not delete another registered installation's settings. No helper stays running.

## Build and validation

Requires Visual Studio 2022 Desktop development with C++, a Windows 11 SDK and CMake 3.24+. No third-party monitoring package is needed.

```powershell
.\Build.ps1
.\CreatePackage.ps1
```

Tests cover calculations, topology, units, premultiplied rendering at 100/125/150/200% DPI, native window behavior, live opacity slider values and keyboard steps, taskbar visibility, background recovery, shortcuts, installed-app registration, startup regression and guarded removal. Window tests require an interactive Explorer desktop and report a skip if unavailable. Installation tests use disposable folders and isolated registry locations; no normal startup entry is modified.

See `docs/validation-1.3.0.md` for measured results and limitations. Historical reports describe their named versions only.

Developer options: `--data-dir PATH` isolates settings and disables startup changes; `--benchmark 180 --warmup 30 --report PATH` measures then exits; `--render-preview PATH` exports illustrative native graphics. Setup accepts `--install-no-launch` for an explicitly requested unattended per-user installation without enabling startup. Normal users should open Setup.exe interactively.
