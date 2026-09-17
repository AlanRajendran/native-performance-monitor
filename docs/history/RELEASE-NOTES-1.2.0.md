## What's new in 1.2.0

- **Compact CPU-core view:** individual 60-second physical-core graphs, grouped and labeled as performance and efficiency cores using Windows topology.
- **One large desktop panel:** CPU, GPU, dedicated VRAM, RAM and the five grouped top applications. This release intentionally removes the small taskbar widget.
- **More translucency:** 25% background opacity by default, with 15% and 40% options in the notification-area menu. Text and graph lines remain crisp; Windows light/dark mode is automatic.
- **Correct decimal units:** GB and MB are converted from byte counters using 1,000,000,000 and 1,000,000 bytes respectively.
- **Visible executables:** PerfMonitor.exe and Uninstall.exe are directly in the portable ZIP's root folder.
- **Background collection:** sampling continues when the panel is covered or hidden. Core-counter handling was improved for low activity.
- **Separate settings:** version 1.1 files, preferences and optional startup registration remain unchanged.

## Download and run

Download **NativePerfMonitor-1.2.0-win-x64.zip**, extract all six files into a new folder and open **PerfMonitor.exe**. Exit the older version first to keep one monitor running. Open **Uninstall.exe** to remove this copy and its owned settings. The build is unsigned; autostart is optional and off by default.

The monitor is native and user-mode only. The separate native uninstall launcher briefly uses built-in Windows PowerShell after confirmation so it can remove both executables. No persistent helper or service is installed.

## Validation

**194 assertions passed.** On the tested Windows 11 system with 14 logical processors, a 180.063-second run after a 30-second warm-up measured **0.0502% average whole-machine CPU** and **38.13 MB median working set**. These are local measurements, not universal guarantees. 99.1% of individual core readings were available; unavailable counter samples remain gaps.

The source ZIP includes code, build scripts and tests. The validation report describes coverage and limitations. SHA256SUMS-1.2.0.txt covers the portable ZIP, source ZIP, report and benchmark files.

## Next version

The next version will address Windows app integration (desktop shortcut and Installed Apps removal), desktop persistence, an optional taskbar strip, and a continuous 10%-100% opacity slider. These features are not part of 1.2.0.
