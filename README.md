# Native Performance Monitor 1.2.0

A portable Windows 11 x64 application with one native monitoring process and one translucent desktop panel. Built with Win32, Direct2D/DirectWrite, DWM, DXGI and user-mode Windows performance counters. No browser runtime, Explorer injection, service, kernel driver or companion monitor.

## Run and control

Extract the portable ZIP and open **PerfMonitor.exe**, directly in its main folder. **Uninstall.exe** is beside it. Keep all six package files together. This is an unsigned build with a static C++ runtime; no additional runtime download or administrator access is required.

The panel starts on the primary monitor behind normal application windows. It starts locked and click-through. Use its notification-area menu (possibly in the overflow) to unlock, move, resize, scroll, show/hide, choose GPU, pause, change opacity, enable optional startup, uninstall or exit. Launching this version again restores its existing instance. There is no taskbar widget in this version. Collection continues while covered or hidden, unless explicitly paused.

Background opacity defaults to 25%, with 15% and 40% alternatives. Text and graph traces remain crisp. Per-pixel alpha composition follows Windows app light/dark mode; high contrast uses opaque system colors. No Windows appearance setting is modified.

## Readings

CPU total, physical cores, GPU, dedicated VRAM and RAM retain 60 one-second samples. Top applications refresh every two seconds. Physical-core rows average their logical processors; all siblings must have valid samples. Windows processor topology supplies core identifiers and efficiency classes. Two classes appear as performance and efficiency groups; homogeneous processors use generic CPU labels, and additional classes retain explicit class numbers rather than guessed marketing names. Core IDs need not be consecutive inside a group.

CPU is busy time and may differ from Task Manager's frequency-adjusted metric. GPU is the busiest engine on the selected adapter. VRAM is dedicated memory on that adapter. Decimal **GB = 1,000,000,000 bytes** and **MB = 1,000,000 bytes** are calculated from byte counters, not relabeled binary values.

The five top applications are grouped by normalized full executable path. Base Windows processes are excluded from ranking but included in system totals. Ranking uses the maximum CPU, GPU, dedicated-memory and physical-memory resource share. This is a usage heuristic, not a stall measurement. Table RAM is private resident working set. Process GPU memory can count shared allocations more than once. Missing counters show dashes and history gaps; integrated GPUs without dedicated capacity show unavailable VRAM. Linked GPU nodes beyond node zero are unsupported.

## Settings and removal

Preferences are isolated in `%LOCALAPPDATA%\NativePerfMonitor-1.2`. Optional autostart uses the current user's Run value `NativePerfMonitor-1.2` and is off by default. Disable it before moving the folder and re-enable from the new location. Windows startup policy may override registration.

Version 1.1 files, settings and startup registration remain untouched. Exit the older version before running this one to keep only one collector active.

Open **Uninstall.exe**, or choose Uninstall in the tray menu. Confirmation is required. It stops only the verified package monitor, removes matching startup registration and owned settings, deletes the six owned package files including both EXEs, and removes empty directories. Unrelated files, downloaded archives and source remain. Invalid ownership markers and redirected paths are rejected.

The native uninstall launcher contains an embedded cleanup script. After confirmation it briefly starts Windows' built-in PowerShell to delete both EXEs, then exits. No script file or persistent helper is installed; system execution policy is unchanged. The monitoring application itself does not use PowerShell.

## Build and tests

Requires Visual Studio 2022 Desktop development with C++, Windows 11 SDK and CMake 3.24+. Validated with MSVC 19.44 and SDK 10.0.26100.0. No third-party monitoring libraries or package downloads.

```powershell
.\Build.ps1
.\CreatePackage.ps1
```

The build runs CoreTests, NativeTests, WindowTests and uninstall fixtures. Window tests need an interactive Explorer desktop; an unavailable desktop is reported as skipped. Close version 1.2 before testing. Tests cover calculations, topology, sibling aggregation, decimal units, settings, rendering at 100/125/150/200% DPI, live cores, background collection, opacity, click-through, single-instance behavior, uninstall cancellation and guarded removal. See `docs/validation-1.2.0.md` for measured results and limitations. Older validation documents describe their named historical versions only.

```powershell
# Isolated preferences; autostart disabled in this mode.
.\PerfMonitor.exe --data-dir "C:\path\to\test-settings"
# Warm up, measure, and exit automatically.
.\PerfMonitor.exe --data-dir "C:\path\to\test-settings" --benchmark 180 --warmup 30 --report "C:\path\to\results.csv"
# Illustrative data rendered by the actual native renderer.
.\PerfMonitor.exe --render-preview "C:\path\to\native-renders"
```

Performance targets are below 0.5% average whole-machine CPU and approximately 50 MB working set. Measurements are machine- and workload-dependent; they are not universal bounds.
