# Native Performance Monitor 1.1.0

A portable native Windows 11 x64 monitor: one executable, two surfaces, one notification-area menu. It uses Win32, Direct2D/DirectWrite, DWM, DXGI, and Windows performance counters. It has no browser runtime, service, kernel driver, telemetry, or companion monitoring process.

## Run

Extract the complete portable ZIP into a writable folder, then open `PerfMonitor.exe`. No installer, administrator privileges, .NET runtime, or additional runtime download is required. The application is an unsigned local build.

The desktop panel starts at the upper-right of the primary monitor. The smaller strip sits **inside a free section of the visible taskbar**, between existing controls. Both start locked and pass clicks to the window or desktop underneath. Use the notification-area icon (possibly inside the overflow menu) for all controls. Double-clicking the executable again restores the existing panel without launching a second collector.

The menu controls surface visibility, locking, moving/resizing, resetting positions, GPU selection, default/compact size, pause, autostart, diagnostics, uninstall, and exit. Unlock to move or resize the panel; a short screen supports scrolling its contents while unlocked. The strip can be moved horizontally and snaps to the nearest available taskbar space. Clear **Inside taskbar** to place it immediately above the bar. Settings follow the primary monitor when display geometry changes. The strip hides for primary-monitor full-screen applications and an auto-hidden taskbar.

Both monitoring surfaces have translucent backgrounds while locked and unlocked: 62% surface opacity, with crisp text and graph lines. Windows app light/dark mode controls the panel; in a mixed Windows theme the strip follows the shell color mode. High contrast uses opaque system colors for legibility. This revision replaces the earlier solid/Mica modes with per-pixel alpha compositing so the backgrounds remain visibly translucent in either lock state. Windows theme, transparency, and taskbar settings are not changed.

The strip is a separate top-level window, not an Explorer plug-in. Read-only UI Automation discovers taskbar controls on a background thread; it never invokes them. It refreshes at most once per second during shell changes and every five seconds at idle. The overlay is normally 344 × 42 DIP, reducing to 280 DIP wide if needed. If no safe gap exists, or Explorer's controls cannot be read, it hides instead of covering buttons. The tray menu can always switch it back above the taskbar. Transient taskbar changes can take up to five seconds to be reflected.

## Readings

Graphs cover 60 seconds and sample once per second. The application table refreshes every two seconds. CPU measures busy time across all logical processors, so it can differ from Task Manager's frequency-adjusted CPU display. GPU is the busiest engine on the selected adapter. VRAM is dedicated memory on that same adapter; shared system memory is not mislabeled as VRAM. Choose the adapter from the tray menu.

Processes are grouped by normalized full executable path. The process count follows each name. Identically named files in different directories and different helper executables remain separate groups. Core Windows processes are excluded from the table, while system graphs include the whole machine. Table RAM is private resident working set. GPU memory is process-attributed and can count shared allocations more than once, so table values need not sum to the adapter graph.

The five rows are ordered by the largest of four shares: CPU percentage, GPU percentage, attributed VRAM / dedicated capacity, and private RAM / physical capacity. This is a resource-share heuristic, not a measurement of stalls or eviction. Unavailable values use a dash. Gaps represent unavailable samples or paused/suspended time. Protected identities are omitted rather than guessed. An integrated GPU with no dedicated capacity reports VRAM as unavailable. Linked physical GPU nodes beyond node zero are explicitly unsupported in this release.

The About / diagnostics menu explains the active adapter and counter status. The two windows expose their current metrics and all application-column values through a read-only UI Automation value.

## Autostart and settings

Autostart is off by default. Enable **Start with Windows** in the tray menu to register only this executable in the current user's `Run` key. Windows Startup settings and organizational policy can override registration. Disable autostart before moving the executable; enable it again from the new location. The registration has Windows' 260-character command-length limit.

Preferences are stored in `%LOCALAPPDATA%\NativePerfMonitor`. No measurement history is stored unless an explicit benchmark report is requested. No Windows appearance, taskbar, security, driver, or power setting is changed.

## Uninstall completely

Choose **Uninstall…** from the tray menu, or run `Uninstall.cmd` in the extracted package. The standalone uninstaller asks you to type `YES`. It stops the verified package process, removes its matching optional startup registration and application-owned settings, deletes the package files and uninstaller, and removes directories only if empty. Unrelated files are preserved. It refuses invalid ownership markers and redirected/reparse-point paths.

The brief PowerShell removal process runs only when uninstalling; it is not a monitoring dependency. Its execution-policy option applies only to that process and does not change Windows execution policy. Windows-managed execution history and separately downloaded ZIP/source archives are outside application-owned cleanup.

## Build and test from source

Requirements: Visual Studio 2022 with Desktop development with C++, a Windows 11 SDK, and CMake 3.24 or newer. The validated compiler is MSVC 19.44 with SDK 10.0.26100.0. No NuGet, npm, or third-party monitoring libraries are required.

```powershell
.\Build.ps1
```

The script builds x64 Release, runs CTest, and runs the uninstaller fixture tests. `CoreTests` tests calculations, grouping, and safe taskbar gap selection; `NativeTests` tests settings, removal boundaries, translucent premultiplied rendering at 100%, 125%, 150%, and 200% DPI; `WindowTests` launches an isolated test instance on the interactive Explorer desktop and tests placement, actual taskbar control avoidance, cross-process hit testing, translucency while unlocked, pause/resume, compact sizing, single-instance behavior, and shutdown. Close a running monitor before running the window test. A desktop without Explorer returns a CTest skip, not a fabricated pass.

To run just the uninstaller fixture tests:

```powershell
.\tests\Test-Uninstaller.ps1 -Executable .\build\Release\PerfMonitor.exe -TestRoot .\build\uninstall-tests
```

After a successful Release build, `.\CreatePackage.ps1` creates the portable ZIP with the uninstaller, ownership marker, and SHA-256 checksums. The source archive contains all code, resources, and tests needed to rebuild it.

Developer-only commands:

```powershell
# Isolated preferences; autostart is disabled in this mode.
.\PerfMonitor.exe --data-dir "C:\path\to\test-settings"

# Two-minute warm-up, ten-minute measurement, then automatic exit.
.\PerfMonitor.exe --data-dir "C:\path\to\test-settings" --benchmark 600 --warmup 120 --report "C:\path\to\results.csv"

# Render deterministic illustrative data using the actual Direct2D renderer.
.\PerfMonitor.exe --render-preview "C:\path\to\native-renders"
```

`--demo` and `--theme light|dark` support isolated visual testing without changing Windows settings. Normal operation uses live data and follows Windows. `--exit` requests the current instance to stop. Build/test executables are development tools and are not included in the portable runtime package.

Performance targets are below 0.5% average whole-machine CPU and around 50 MB working set. Consult the delivered validation report for actual measurements, hardware, and test limitations; these are not guaranteed bounds across every process count, DPI, driver, or Windows build.
