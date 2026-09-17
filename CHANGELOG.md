# Changes

## 1.3.0

- Add per-user Setup.exe with desktop and Start menu shortcuts and Windows Installed Apps registration.
- Remove matching shortcuts and registration during guarded uninstall.
- Restore the optional taskbar strip, enabled by default, with an above-taskbar fallback when no safe internal gap exists.
- Add a native 10%-100% panel-opacity slider, live updates, persistence and one-percent keyboard steps.
- Recover the locked desktop panel after unexpected hiding/minimization and repair stacking behind ordinary app windows.
- Fix disabling the version-specific Windows startup entry.
- Add installation, removal, slider and persistence regression coverage.

## 1.2.0

- Replace the two-surface layout with one large compact-core desktop panel.
- Discover physical cores and efficiency classes from Windows; retain individual 60-second histories.
- Continue sampling while the panel is covered or hidden.
- Convert memory counters to decimal GB and MB.
- Reduce default background opacity to 25%, with 15% and 40% menu options.
- Put PerfMonitor.exe and the native Uninstall.exe launcher directly in the portable folder and ZIP root.
- Isolate this version's settings and optional startup registration from version 1.1.
- Expand automated tests for topology, live core readings, background collection, opacity and uninstall boundaries.
## 1.1.0

- Place the four-graph strip inside unused taskbar space by default, with an option to return above it.
- Detect taskbar control bounds through read-only, cached UI Automation on a background thread.
- Use translucent surfaces in both lock states while keeping text and graph traces readable.
- Preserve click-through while locked and native dragging/resizing while unlocked.
- Add tests for taskbar gap selection, control avoidance, and premultiplied alpha.
- Fix PNG exports to convert premultiplied pixels into straight alpha correctly.

## 1.0.0

Initial native Windows x64 release with one-second graphs, two-second grouped process ranking, optional per-user startup, portable packaging, and guarded uninstall.

