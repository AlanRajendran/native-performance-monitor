# Native Performance Monitor 1.3.0 validation

Completed 11 September 2026 on Windows 11 x64, Intel Core Ultra 5 245KF (14 physical/logical cores), NVIDIA RTX 5070 Ti, primary display 3440 x 1440 at 96 DPI. Built with MSVC 19.44.35219 and Windows SDK 10.0.26100.0, x64 Release/static CRT.

## Automated checks

283 assertions passed:

- 49 core calculations/history/grouping assertions.
- 119 native assertions for topology, units, settings, and premultiplied rendering of both surfaces at 100/125/150/200% DPI.
- 69 interactive window assertions including live cores, 10..100 opacity range, intermediate value 57%, one-percent keyboard changes, taskbar toggle, unexpected hide/minimize recovery, behind-ordinary-window stacking, background sampling, pause, single instance and shutdown.
- 28 installation assertions covering real shortcut targets, per-user uninstall metadata, collision rejection, actual EXE uninstall, shortcut/registration cleanup, unrelated-file preservation and the startup-disable regression.
- 18 standalone uninstall assertions covering running-process removal, ownership validation, redirected paths and preservation of unrelated files.

The full CTest run passed all four suites on the interactive desktop; no tests were skipped. Following the final uninstall payload adjustment, installation and standalone uninstall tests passed again. Restricted sandbox registry visibility differs from the signed-in account; Windows integration checks were performed in that actual account using disposable fixture keys.

## Measured overhead

A 180.313-second measurement after 30 seconds of warm-up collected 180 samples.

| Metric | Result |
|---|---:|
| Average whole-machine CPU used by monitor | 0.0563% |
| Single-core equivalent CPU | 0.7886% |
| Median working set | 44.24 MB |
| 95th percentile working set | 44.71 MB |
| Peak sampled working set | 44.77 MB |
| Peak private bytes | 25.03 MB |

Valid core readings: 2520/2520 (100.0%). Raw measurements are supplied in benchmark-1.3.0.csv; the CSV has a trailing summary section. These local measurements meet the requested average CPU and approximate working-memory targets.

Both widgets were enabled, locked, at 96 DPI. Measurement covers only the monitor's process CPU time and working set. These are short local measurements, not universal upper bounds. Counter gaps remain explicit.

## Installed app verification

The per-user Setup.exe completed successfully. Desktop and Start menu shortcuts were read back and resolve to the installed PerfMonitor.exe. The actual HKCU Installed Apps entry reports Native Performance Monitor 1.3, version 1.3.0, and a correctly quoted Uninstall.exe command. No administrator installation, service or driver was added. The installed app was launched successfully through its actual desktop shortcut. The user's existing enabled v1.2 autostart preference was moved to the exact installed v1.3 executable; the old registration was removed after verifying its target. The old monitor exited gracefully, leaving one running v1.3 monitor. This migration was performed for this installation; Setup itself leaves older versions' startup entries unchanged.

## Limits

The taskbar overlay uses free space only when taskbar bounds are reliable and otherwise appears above the bar. It may temporarily hide for full-screen apps, auto-hide or overlapping shell menus. Tests simulate shell recreation messages; they do not terminate Explorer or reboot Windows. Physical multi-monitor transitions and every driver/shell customization remain untested. Static native previews use illustrative data. The binary is unsigned. A transient built-in PowerShell process performs confirmed self-removal; it is not a companion monitor.

Version 1.2 was published separately on GitHub before this work. Version 1.3 is a new local build and installation.

Packaged PerfMonitor.exe SHA-256: `c33491feab5f035446187a08a6f1f65aa84cd155a756fd649df79314c500c81c`.
