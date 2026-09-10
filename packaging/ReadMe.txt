NATIVE PERFORMANCE MONITOR 1.2.0

OPEN PerfMonitor.exe in this folder.
REMOVE this version by opening Uninstall.exe in this same folder.
Keep all six package files together. Neither EXE is hidden or in a subfolder.

One native monitoring process, one large translucent desktop panel.
No small taskbar widget. The panel stays behind ordinary application windows.
CPU total and each physical core have 60-second histories; P/E labels are
discovered from Windows. GPU, dedicated VRAM, RAM and grouped top applications
remain visible. Collection continues while the panel is covered or hidden.

GB means 1,000,000,000 bytes; MB means 1,000,000 bytes. Values are converted
from byte counters. Memory capacities may look different from binary units.

Use the notification-area icon (possibly in the overflow menu) to unlock,
move, resize, hide/show, pause, select GPU, adjust opacity or exit.
Locked panels pass clicks through. Unlock and scroll on a short screen.
Default background opacity is 25%; the menu also offers 15% and 40%.
Text and graph lines remain opaque. Windows light/dark theme is automatic;
high contrast uses solid system colors for readability.

Version 1.2 has separate settings in %LOCALAPPDATA%\NativePerfMonitor-1.2.
It does not change version 1.1 files, preferences or startup registration.
Exit the old version before running this one if you want only one monitor.
Start with Windows is optional, off by default, and requires no administrator.

Uninstall.exe asks for confirmation. It removes this copy's files, settings
and matching optional startup entry; unrelated files and version 1.1 remain.
The uninstall EXE contains its cleanup script and briefly uses Windows'
built-in PowerShell after confirmation so it can delete both EXEs. No script
file, persistent helper, service or scheduled task is installed. The monitor
itself has no PowerShell dependency. No system execution policy is changed.

This is an unsigned portable Windows 11 x64 build. No installer or additional
runtime download is needed. Source and tests are supplied separately.
