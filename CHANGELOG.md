# Changes

## 1.6.0

A new look, chosen from five designs.

### Desktop panel

- **Gauges for CPU, GPU and memory.** A ring for each shows the current load,
  with the processor model, graphics card or memory used underneath. Under
  every ring, a strip of heat cells shows the selected history range.
- **Cores as a heat map instead of line graphs.** One row per physical core,
  one cell per sample, performance cores then efficiency cores. Load shows as
  colour, so which cores are busy and when can be read at a glance, and the
  map stays compact on CPUs with many cores.
- **Peaks use the Windows accent colour.** The heat map, gauges and highlights
  take the accent from Windows, which follows the wallpaper when the accent is
  set to Automatic. It updates as soon as the accent changes.
- **Working hardest** replaces the application table: the four busiest apps
  with their CPU, GPU, VRAM and RAM side by side, and the largest share of its
  capacity highlighted.
- The default panel is now 450 × 760.

### Taskbar strip

- Fixed-width readings. CPU shows one bar per physical core; GPU, VRAM and
  RAM show a twelve-step meter in the accent colour.

### Release line

- Installs as its own line (`NativePerfMonitor-1.6`) beside earlier versions
  and imports the 1.5 settings on first run.


## 1.5.0

A redesign of how the panel and strip stay in place, after 1.4 still flickered
and lost both surfaces. Every change below is grounded in measurements taken on
Windows 11 25H2 with a z-order recorder; see docs/placement.md.

### Stability

- **The strip no longer blinks when the taskbar is clicked.** Explorer raises
  the taskbar over every other topmost window whenever it is activated, and 1.4
  could only put the strip back afterwards — 246 ms of blink on every click. The
  strip is now owned by the taskbar, so the window manager carries it along in
  the same operation: measured 0 ms.
- **Show Desktop no longer loses the panel.** Windows 11 raises the desktop over
  every application window and silently refuses to let an ordinary window above
  it; every repair 1.3 and 1.4 attempted reported success and changed nothing.
  The panel now detects Show Desktop and becomes temporarily topmost, at the
  bottom of the topmost band, then drops back into the desktop layer. This is the
  technique Rainmeter uses for its "On Desktop" skins.
- **Nothing can push the locked panel around.** It refuses every z-order change
  it did not make itself, so it no longer needs watching or repairing.
- **The strip recovers from Show Desktop too**, which repositions the taskbar in
  a way that bypasses ownership. It checks on every foreground change and every
  250 ms, and moves only when it is actually covered.
- **The strip runs on its own thread.** Ownership ties that thread's input to
  the taskbar's, so it does nothing but place and draw the strip; nothing else
  in the program can ever hold up the taskbar.
- The system-wide event hook is down to foreground and minimize events.
- The strip no longer hides for shell menus and flyouts; as an owned window it
  sits directly above the taskbar and they naturally open above it.
- The strip waits for its taskbar slot at launch instead of appearing above the
  taskbar and jumping inside.

### Features

- **Record placement trace** in the tray menu (and `--trace`) writes every
  placement action, and why, to `trace.log` — so a future report comes with a
  record of what happened.
- Settings from the previous version are imported on first run, read-only.

### Development

- New files: `src/striphost.{h,cpp}`, `src/accessibility.h`, `src/trace.{h,cpp}`.
- Window tests assert the new guarantees: the strip is owned by the taskbar, the
  locked panel refuses restacking by another process, and the locked strip
  passes the pointer through to the taskbar.
- Test for importing the previous version's settings.

## 1.4.0

Installs alongside 1.3 rather than upgrading it, as every previous release in
this project has done. 1.4 has its own install folder, settings directory,
shortcuts, startup entry and Installed Apps registration, so the two cannot
interfere with each other and either can be removed independently. Settings do
not carry over; remove 1.3 from Windows Installed apps once 1.4 is set up the
way you want it.

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
- Put the surfaces back when something else buries them. Z-order is shared
  state that any process can change silently, so it is now checked against what
  is actually on screen every pass rather than assumed from the last request.
  Previously a surface pushed behind the wallpaper host or the taskbar stayed
  there, invisible, until the tray menu toggle forced a fresh placement.
- Never send the panel to the bottom of the z-order when the desktop host
  cannot be identified, which happens while the wallpaper host is recreated.
  Leaving the stacking alone is always safer than burying the window.

### Appearance

- The taskbar strip now looks embedded in the taskbar instead of like a card
  resting on it: inside the bar it draws no background, no border, no rounded
  corners and no cell dividers, and its graph fills tint the bar rather than
  covering it. Windows' own window border is turned off for both surfaces.
  Above the taskbar the strip keeps its plate, where it needs one.

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

