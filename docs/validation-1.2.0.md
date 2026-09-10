# Native Performance Monitor 1.2.0 validation

Built and checked on 10 September 2026. This report applies to the supplied 1.2.0 binaries, not the historical 1.1 report.

## Scope delivered

One large background desktop panel with compact physical-core graphs and Windows-reported P/E grouping; no taskbar widget. Decimal GB/MB conversion, 25% default background opacity (15/40% alternatives), automatic light/dark, GPU/VRAM/RAM graphs and five grouped application rows. Both PerfMonitor.exe and Uninstall.exe are visible files directly in the new folder and portable ZIP root.

## Test environment

Windows 11 x64, Intel Core Ultra 5 245KF, 14 physical/logical processors, NVIDIA RTX 5070 Ti. Primary display 3440 x 1440 at 96 DPI. Windows topology reports P cores 0, 1, 10, 11, 12, 13 and E cores 2 through 9. Physical memory reported by Windows: 34.03 decimal GB.

MSVC 19.44.35219, Windows SDK 10.0.26100.0, CMake 4.3.3, x64 Release with static CRT.

## Automated verification

- 49 core assertions: history, calculations, grouping and existing geometry helpers.
- 75 native assertions: topology mapping, sibling aggregation, units, settings, ownership and rendering including alpha at 100%, 125%, 150% and 200% DPI.
- 52 interactive window assertions: one panel/no strip, all 14 live core readings, collection while covered and hidden, primary placement, locked click-through, unlocked interaction, compact layout, opacity persistence, pause/resume, single instance, shutdown and uninstall confirmation cancellation.
- 18 uninstall assertions: complete removal including both EXEs, unrelated-file preservation, invalid identity rejection, redirected-path rejection and stopping only the matching package monitor.

**194 assertions passed.** The final CTest run passed all three suites on the interactive desktop; none were skipped. The uninstaller fixture tests used isolated settings and did not alter autostart registration.

Additional final-package checks verified exactly six ZIP-root entries, all listed SHA-256 hashes, x64 PE headers, exact matches with tested build binaries, version 1.2.0 metadata and visible file attributes. A fresh extracted copy was removed using its own Uninstall.exe; both EXEs, owned settings and the empty package directory were removed successfully. The delivered folder was preserved.

## Resource measurement

The final binary ran with its live, visible, locked panel at 96 DPI for 180.063 seconds after 30 seconds of warm-up (180 samples).

| Metric | Result |
|---|---:|
| Average whole-machine CPU used by the monitor | 0.0502% |
| Single-core equivalent CPU | 0.7029% |
| Median working set | 38.13 MB |
| 95th percentile / peak sampled working set | 38.14 MB |
| Peak private bytes | 20.57 MB |

These local results meet the requested average CPU and approximate working-memory targets. All 14 cores were available in 166 of 180 samples; the remaining samples had 11-13 valid cores. In total, 2,498 of 2,520 core readings were valid (99.1%). No sample lost all cores. Unavailable readings remain honest gaps. The report includes the raw CSV and benchmark summary; the CSV has a trailing summary section, so parsers should select measurement rows.

The delivered monitoring binary SHA-256 is `ae2fe43b4adbbc4e2a784b26cf13684ef967e2e632e08cba6a8d0c4ebba74411`.

The collector uses valid idle-time counters to calculate busy time. Occasional unavailable Windows counter samples remain visible as gaps; they are never filled with fabricated activity. Timing resolution, active process count, GPU driver and other machine workloads can affect results.

## Visual and operational limits

Native dark and light previews were inspected, including core labels, graph layout and decimal units. Exported images use illustrative data and retain transparent pixels; image viewers may composite them against a solid background. Foreground text and traces are intentionally crisp. High contrast uses a solid background.

Rendering was tested at four DPI scales; physical multi-monitor transitions and every Windows theme/driver combination were not exhaustively tested. The live CPU has no SMT; sibling aggregation was tested synthetically. Homogeneous and more-than-two-class CPU labels were not validated on separate hardware. Very high core counts require scrolling while unlocked. PDH readings may differ from Task Manager frequency-adjusted utilization. Performance results are a short local sample, not a universal guarantee.

Version 1.1 and normal user startup settings were not changed. No GitHub publication was performed. The monitor is an unsigned portable EXE. Its distinct native uninstall launcher invokes Windows' built-in PowerShell briefly after confirmation to remove itself and owned files; it is not an additional background monitor.
