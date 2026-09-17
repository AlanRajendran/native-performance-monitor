NATIVE PERFORMANCE MONITOR 1.4.0

For normal Windows app integration, open Setup.exe in this folder.
It installs only for your account, adds desktop and Start menu shortcuts,
and registers Native Performance Monitor 1.4 in Windows Installed Apps.
Administrator access is not required. The sign-in startup checkbox is optional.

For portable use, open PerfMonitor.exe directly. No shortcuts or Installed Apps
entry are created in portable mode. Keep all seven package files together.
PerfMonitor.exe, Setup.exe and Uninstall.exe are visible in the main folder.

The large desktop panel has compact physical-core graphs, P/E core labels,
GPU, dedicated VRAM, RAM and five grouped application rows. Sampling continues
when covered or hidden. Locked surfaces pass clicks through. The desktop panel
is kept behind ordinary application windows and recovers from unexpected
minimization/hiding. It remains controlled through the notification-area icon.

The small taskbar strip is ON by default and can be toggled in the tray menu.
It uses unused taskbar space when reliable; otherwise it appears immediately
above the bar. Full-screen applications and shell menus can temporarily hide
it. It never injects into or replaces Explorer.

Right-click the notification-area icon, then choose Panel opacity.
Use the continuous slider from 10% to 100% (opaque). Arrow keys change 1%.
The panel updates live; the setting is saved. Text and graph lines stay crisp.
The taskbar strip keeps its own readable translucent background.
Windows light/dark theme is automatic. High contrast uses solid system colors.

GB means 1,000,000,000 bytes; MB means 1,000,000 bytes.
Settings are in %LOCALAPPDATA%\NativePerfMonitor-1.4.
Installed files are in %LOCALAPPDATA%\Programs\NativePerfMonitor-1.4.
Earlier versions are kept. Exit an older monitor before running this version.

Remove an installed copy through Windows Settings > Apps > Installed apps,
or run Uninstall.exe. It confirms removal, stops the matching monitor and
removes its owned files, settings, shortcuts and matching registration.
Unrelated files and earlier versions remain. A transient built-in PowerShell
process performs self-removal after confirmation; no helper stays running.

This unsigned native Windows 11 x64 build requires no extra runtime download.
Source, automated tests and a measured validation report are supplied separately.
