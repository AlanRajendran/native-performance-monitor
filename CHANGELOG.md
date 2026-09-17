# Changes

## 1.4.0

Upgrades an existing 1.3 installation in place. Settings, the install location
and the Installed Apps registration are unchanged, so nothing needs removing
first and no setting is lost.

### Stability

- Stop the desktop panel and taskbar strip flickering. Placement now compares
  what it wants against what the shell was last told and does nothing when they
  match, instead of reapplying position, stacking and visibility every second.
- Stop the strip hopping between taskbar slots. It keeps the slot it occupies
  unless that slot is genuinely taken, ignores drift under 12px, and moves at
  most once every 1.5 seconds. Ordinary taskbar activity — an app opening, a
  notification badge, the weather widget reflowing — no longer moves it.
- Stop the strip blinking as windows pass over it. It hides immediately so shell
  menus are never covered, but waits for the obstruction to stay gone before
  coming back.
- Narrow the system-wide event hook from ten event types to three. It previously
  received focus, selection, state-change and location-change events from every
  window in every process, which is a continuous stream while anything is being
  dragged, resized or animated.
- Suspend z-order repair while a full-screen application is in the foreground,
  and rate limit it otherwise. Repeatedly reordering windows could drop an
  exclusive full-screen game out of its presentation mode.
- Stop re-asserting the strip above the taskbar every second, which could make
  Explorer re-assert in turn.
- Read the taskbar layout only while the strip is actually being fitted inside
  it, and far less often. This work drives Explorer's own UI thread and was a
  measurable cost on slower machines.
- Stop rewriting the accessibility text every second when it had not changed;
  each write broadcast a name-change event system wide.

### Features

- **History** in the tray menu switches every graph between **60 seconds** and
  **60 minutes**, on both the panel and the strip. The hour view shows
  one-minute averages and its newest point updates every second. Both ranges are
  recorded continuously, so switching is instant.
- Panel opacity now applies to the background only. Text, traces, grid lines and
  the graph cards are always fully opaque, so content stays readable at any
  opacity and over any wallpaper. Large graphs sit on solid plot cards.

### Development

- Repository reorganised: source at the root, build scripts in `scripts/`,
  reference material in `artifacts/`, and real documentation in `docs/` covering
  the architecture, the placement rules, the rendering model and troubleshooting.
- `--render-preview` now exports both themes, both surfaces, both history ranges
  and three opacity settings.
- Added regression coverage for minute aggregation, partial minutes, missing
  metrics and long gaps.

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

