# Native Performance Monitor 1.1.0 validation

The x64 Release build passed 200 automated assertions. This revision places the small overlay inside available taskbar space and keeps both surface backgrounds translucent while locked and unlocked. The original approved architecture and 1.0 validation report are retained as historical documents; the 1.1 README and changelog describe the current behavior.

## Automated checks

| Suite | Result | Coverage |
|---|---:|---|
| CoreTests | 49 assertions passed | Rate normalization, history, grouping and ranking, primary-monitor geometry, and taskbar free-space selection |
| NativeTests | 101 assertions passed | DXGI enumeration, guarded settings, startup command construction, rendering at 100/125/150/200% DPI, and premultiplied translucent pixels |
| WindowTests | 33 assertions passed | Two same-process surfaces, placement behind ordinary windows, actual taskbar bounds/control avoidance, cross-process click-through, unlocked translucency, compact sizing, pause/resume, duplicate launch and shutdown |
| Uninstaller fixtures | 17 assertions passed | Complete owned-file removal, unrelated-file preservation, invalid marker and junction rejection, verified running-process exit and self-removal |

All three CTest suites passed on the interactive Explorer desktop in 10.40 seconds. The uninstaller fixtures also passed. Tests used isolated settings and did not enable autostart or change Windows appearance, security, display, or taskbar settings. Tests without an Explorer desktop explicitly skip the native-window suite.

## Performance measurement

| Measurement | Result |
|---|---:|
| Warm-up | 30 seconds |
| Measurement duration | 180.844 seconds |
| Samples | 180 |
| Average whole-machine CPU used by the app | 0.0277716% |
| Single-core equivalent | 0.388802% |
| Median working set | 41,299,968 bytes (41.30 MB) |
| 95th-percentile working set | 41,361,408 bytes |
| Peak sampled working set | 41,365,504 bytes |
| Peak private bytes | 21,762,048 bytes |
| Executable size | 540,672 bytes |

Measured on Windows 11 Pro 25H2 build 26200.9168, with 14 logical processors, NVIDIA GeForce RTX 5070 Ti, and a 3440 x 1440 primary display at 100% DPI. Both surfaces were enabled and locked. This three-minute measurement describes the monitor's own overhead; other applications were running and the entire computer was not held at controlled idle. The desktop panel could be occluded. Surface settings are recorded, not continuous unobstructed visibility. These results meet the requested targets on this machine, but are not universal bounds.

CPU uses process kernel/user time deltas divided by elapsed time and logical processor count. Working set is sampled once per second with GetProcessMemoryInfo. The release includes this report and package checksums; the packaged executable is byte-identical to the tested benchmark binary.

## Build and packaging

Built with MSVC 19.44.35219, Windows SDK 10.0.26100.0 and CMake 4.3.3 using C++20 and the static C++ runtime. Runtime dependencies are Windows system DLLs. The application is unsigned and requests asInvoker privileges and PerMonitorV2 DPI awareness.

The portable ZIP contains exactly PerfMonitor.exe, README.md, LICENSE.txt, Uninstall.cmd, Uninstall.ps1, .nativeperf-package and SHA256SUMS.txt. The source ZIP includes code, resources, build scripts and automated tests. Keep the full portable folder together. No installer, service, driver or companion monitoring program is included. The PowerShell uninstaller runs only on request, validates ownership, removes matching application files/settings/startup registration and preserves unrelated files.

## Validation limits

Taskbar controls are discovered through read-only UI Automation on a background thread. Changes can take up to five seconds to be reflected. The strip hides if there is no reliable free gap; the tray menu can switch it above the bar. This is an independent overlay inside the visible taskbar rectangle, without Explorer injection or replacement.

Per-pixel alpha replaces 1.0's Mica/solid modes. High contrast intentionally uses opaque system colors. The rendered PNGs use illustrative data from the native renderer and are not live telemetry screenshots.

Physical DPI switching, multiple physical monitors, virtual desktops, Show Desktop, auto-hide/full-screen transitions, Explorer restart, suspend/resume, high contrast, automatic theme switching and GPU device removal were not all exercised end to end. Render tests at 200% DPI do not establish performance at that DPI. Hybrid/eGPU/UMA and multi-processor-group systems were not available. Linked GPU nodes beyond physical node zero remain unsupported. Protected metadata and missing counters are displayed as unavailable; process GPU-memory attribution can overlap. Application ranking is a resource-share heuristic.

Autostart command construction and cleanup boundaries were tested without changing a real startup registration; login launch and removal of an enabled registration were not tested on the user's configuration.

Executable SHA-256: `72d4b4099571c332b2195721e64aff4ce925b14d35918899fe2bd41c4669e312`
