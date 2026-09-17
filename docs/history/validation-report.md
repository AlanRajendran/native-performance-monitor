# Native Performance Monitor 1.0.0 — validation

The Windows x64 Release build passed the automated calculation, native rendering/storage, native window, and uninstaller suites. The measured monitor overhead met both requested performance targets on this machine.

## Performance measurement

| Measurement | Result |
|---|---:|
| Warm-up | 120 seconds |
| Measurement duration | 600.515 seconds |
| Recorded samples | 598 |
| Average whole-machine CPU used by this app | 0.0677% |
| Single-core equivalent CPU | 0.947% |
| Median working set | 38.04 MB (36.28 MiB) |
| 95th-percentile working set | 39.52 MB |
| Peak sampled working set | 39.61 MB |
| Peak private bytes | 26.58 MB |
| Executable size | 527,872 bytes |

This was a live desktop run on Windows 11 Pro 25H2, build 26200.9168, with 14 logical processors, an NVIDIA GeForce RTX 5070 Ti, and a 3440 × 1440 primary monitor at 100% DPI. Both surfaces were enabled and locked. Normal applications were running; the rest of the PC was not held at a controlled idle. Windows could occlude the desktop panel and its implementation skips fully occluded repaints. The report records surface settings, not a continuous log of actual unobstructed visibility. Collection continued at one second and process ranking at two seconds. These numbers describe the app's own overhead, normalized across all 14 logical processors. They are not a universal hardware guarantee or a benchmark with the unlocked Mica window continuously exposed.

CPU was computed from the app's kernel/user process-time delta over measured elapsed time using GetProcessTimes. Working set came from GetProcessMemoryInfo once per second. The CSV includes handles, GDI/USER resources, system metrics, and eligible-process counts. The final benchmark binary and the packaged executable have the same SHA-256 hash.

## Automated checks

| Suite | Result | Coverage |
|---|---:|---|
| CoreTests | 43 assertions passed | Rate normalization, history/gaps, GPU engine aggregation, path grouping, process exclusions, ranking and geometry |
| NativeTests | 69 assertions passed | Real DXGI enumeration, guarded settings storage/removal, startup command construction, Direct2D rendering at 100/125/150/200% DPI, compact and scrolled layouts |
| WindowTests | 29 assertions passed | Two same-process surfaces, primary bounds, placement above the desktop and below ordinary windows, taskbar gap, real cross-process click-through, unlock/relock targeting, no focus steal, 360 × 460 compact sizing, pause/resume, duplicate launch and shutdown |
| PowerShell uninstaller fixtures | 17 assertions passed | Full removal, unrelated-file preservation, ownership-marker rejection, junction rejection, running-process exit and self-removal; paths include spaces, apostrophe and ampersand |

The three CTest suites passed on the interactive Explorer desktop. WindowTests deliberately skips on a desktop without Explorer. Isolated settings under the workspace were used throughout; tests did not enable startup or change Windows theme, taskbar, display, security, or power settings. The benchmark exited automatically.

Unlocked Mica requests were accepted by DWM (backdrop attribute 2). Locked surfaces use the documented solid fallback for cross-process click-through. The delivered PNGs are deterministic sample data drawn by the actual native renderer, not screenshots of current live measurements or proof of the compositor's optical Mica effect.

## Build and package verification

Built with MSVC 19.44.35219, the Windows SDK 10.0.26100.0, and CMake 4.3.3, using C++20 and the static C++ runtime. The executable imports only Windows system DLLs. Its embedded manifest requests asInvoker privileges and PerMonitorV2 DPI awareness. The binary is unsigned. No browser runtime, monitoring library, kernel driver, service, runtime installer, or always-running helper is shipped.

Portable contents: PerfMonitor.exe, README.md, LICENSE.txt, Uninstall.cmd, Uninstall.ps1, .nativeperf-package, and SHA256SUMS.txt. Keep the whole extracted folder together. The uninstaller only runs on request and removes exact owned files and its matching optional per-user startup value. It preserves unrelated files and refuses reparse paths. Windows-managed history and separately downloaded archives remain outside application-owned cleanup.

Executable SHA-256: `ab8ef04a388948ed111d3026c369b6ff95d90b10e23b551435441085e3b86589`

## Remaining validation limits

Physical DPI switching, multiple physical monitors, virtual desktops, Show Desktop, auto-hide/full-screen transitions, Explorer restart, GPU driver reset/device removal, suspend/resume, high contrast and automatic theme switching have implementation paths but were not all exercised end to end on this desktop. Render tests at 200% DPI are not a 200% DPI performance measurement. Hybrid/eGPU/UMA hardware and processors spanning more than one processor group were not available for hardware testing. Linked GPU nodes beyond physical node zero are explicitly unsupported. Protected process metadata may be unavailable; missing counter values are shown as gaps/dashes. The ranking is a resource-share heuristic, and process-attributed GPU memory can overlap. UI Automation exposes each surface as one read-only value with its metrics and rows, rather than a cell-by-cell data grid.

Autostart command construction and safe cleanup logic were tested without writing a real startup registration. Login launch and removal of an enabled registration have not been exercised on the user's configuration.
