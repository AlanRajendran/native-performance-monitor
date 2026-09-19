NATIVE PERFORMANCE MONITOR 1.7.0

For normal Windows app integration, open Setup.exe in this folder.
It installs only for your account, adds desktop and Start menu shortcuts,
and registers Native Performance Monitor 1.7 in Windows Installed Apps.
Administrator access is not required. The sign-in startup checkbox is optional.

For portable use, open PerfMonitor.exe directly. No shortcuts or Installed Apps
entry are created in portable mode. Keep all seven package files together.
PerfMonitor.exe, Setup.exe and Uninstall.exe are visible in the main folder.

The large desktop panel has compact physical-core graphs, P/E core labels,
GPU, dedicated VRAM, RAM and five grouped application rows. Sampling continues
when covered or hidden. Locked surfaces pass clicks through. The locked panel
sits in the desktop layer, behind ordinary application windows, and stays
visible during Show Desktop. It is controlled through the notification-area icon.

The small taskbar strip is ON by default and can be toggled in the tray menu.
It uses unused taskbar space when reliable; otherwise it appears immediately
above the bar. Inside the taskbar it is owned by the taskbar window, so clicking
the taskbar never covers it. Full-screen applications temporarily hide it. It
never injects into or replaces Explorer.

Right-click the notification-area icon, then choose Panel opacity.
Use the continuous slider from 10% to 100% (opaque). Arrow keys change 1%.
The panel updates live; the setting is saved. Opacity affects the background
only: text, graphs and grid lines always stay fully opaque. History in the same
menu switches every graph between 60 seconds and 60 minutes.
Windows light/dark theme is automatic. High contrast uses solid system colors.

GB means 1,000,000,000 bytes; MB means 1,000,000 bytes.
Settings are in %LOCALAPPDATA%\NativePerfMonitor-1.7.
Installed files are in %LOCALAPPDATA%\Programs\NativePerfMonitor-1.7.
Earlier versions are kept, and their settings are imported on first run. Exit
an older monitor before running this version, and remove it from Installed apps
once this one is set up so its shortcuts cannot start the old build.

If anything misbehaves, choose Record placement trace in the tray menu,
reproduce the problem, then choose it again. trace.log in the settings folder
records what the monitor did and why.

Remove an installed copy through Windows Settings > Apps > Installed apps,
or run Uninstall.exe. It confirms removal, stops the matching monitor and
removes its owned files, settings, shortcuts and matching registration.
Unrelated files and earlier versions remain. A transient built-in PowerShell
process performs self-removal after confirmation; no helper stays running.

This unsigned native Windows 11 x64 build requires no extra runtime download.
Source, automated tests and a measured validation report are supplied separately.
