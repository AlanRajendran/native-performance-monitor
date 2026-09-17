# Version 1.3 architecture

Version 1.3 adds Windows integration and restores the taskbar surface while retaining the native physical-core collector described in architecture-1.2.0.md.

## Runtime and desktop persistence

One PerfMonitor.exe process owns a notification controller, desktop panel and optional taskbar strip. A user-mode collector samples once per second; ranking updates every two samples. A separate thread within the same process reads cached taskbar accessibility bounds, never invoking Explorer controls or injecting code.

Shell notifications trigger placement updates. A once-per-second reconciliation repairs incorrect desktop stacking and unexpected hiding/minimization. Locked panels ignore minimize commands. Explicit tray visibility and pause settings are respected. Geometry requests during an existing placement operation are deferred rather than discarded. Collection does not depend on window visibility. The taskbar strip defaults on, prefers a reliable unoccupied slot inside the primary taskbar, and otherwise stays immediately above it. Full-screen apps, auto-hide and overlapping shell menus can temporarily suppress it.

## Opacity control

The tray's Panel opacity command opens a native modeless Win32 window containing a standard trackbar. Its range is 10..100 with one-percent keyboard increments. Changes update the panel immediately and save on release/keyboard adjustment/close. Text and graph traces remain opaque. The strip retains a separate 62% background for readability. The control follows app light/dark colors; high contrast uses system colors.

## Per-user installation

Setup.exe copies seven package files to LocalAppData/Programs/NativePerfMonitor-1.3. It uses IShellLinkW to create desktop and Start menu shortcuts and the current-user Uninstall registry branch to register display name, version, icon, location and quoted Uninstall.exe command. No elevation, service, scheduled task or additional monitoring process is required. Sign-in startup is an optional checkbox. Portable use does not register Windows integration.

Installation checks for redirected paths, required package files, conflicting shortcuts and existing registration ownership. It never overwrites another installed copy from a different source location. New files, shortcuts and registration are rolled back on failure. Reopening Setup from the installed directory can recreate missing owned integration.

The native uninstall launcher confirms removal and runs its embedded Windows PowerShell payload briefly for self-deletion. The payload checks package/setting identity and executable metadata, stops only the matching monitor, deletes exact owned files, resolves shortcut targets before deleting them, and removes only the matching uninstall/startup registration. It preserves unrelated files and an installed copy's settings when removing a separate portable copy. Test-only integration arguments are restricted to isolated GUID-named fixtures and a separate registry namespace.

## Verification boundaries

Automated tests exercise real shortcuts, disposable registry entries, the actual uninstall EXE, slider endpoints/keyboard movement, live cores, hide/minimize recovery, stacking behind an ordinary window and simulated Explorer recreation notifications. Render tests cover four DPI scales and both surfaces. Physical sleep/resume, actual Explorer termination, Windows reboot and every possible third-party taskbar customization are not exhaustively exercised.

References: Microsoft ShowWindow documentation and Uninstall registry-key properties:
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow
- https://learn.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key
