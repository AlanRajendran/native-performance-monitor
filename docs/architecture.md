# Architecture

Native Performance Monitor is a single Win32 process with no service, no driver,
no elevation and no helper processes. It samples system performance counters
once a second and draws two always-available surfaces: a desktop panel and a
taskbar strip.

Per-version design notes for 1.1–1.3 are kept in [history/](history/).

## Processes and threads

```
PerfMonitor.exe
├── UI thread ........ controller window, panel, strip, tray icon, menus,
│                      all rendering, all placement
├── Collector thread . PDH and DXGI sampling on a 1 s waitable timer (MTA)
└── Taskbar thread ... UI Automation reads of the taskbar layout (MTA)
```

Worker threads never touch a window. They publish an immutable snapshot under a
mutex and `PostMessageW` a notification; the UI thread picks it up on its own
schedule. This is the reason a slow counter query or a slow accessibility walk
cannot stall drawing.

`Setup.exe` and `Uninstall.exe` are separate executables that run only when
invoked. They are not part of the monitor's runtime.

## Windows

| Window     | Class suffix   | Purpose                                        |
| ---------- | -------------- | ---------------------------------------------- |
| controller | `.Controller.` | message sink; owns the tray icon, timers, hooks |
| panel      | `.Surface.`    | the desktop panel                              |
| strip      | `.Surface.`    | the taskbar strip                              |

The controller is never visible. Keeping it separate means the surfaces can be
shown, hidden, restyled or recreated without affecting timers, the tray icon or
the single-instance mutex.

Both surfaces are layered, tool-window, and click-through while locked. See
[rendering.md](rendering.md) for how they are drawn and
[placement.md](placement.md) for how they are positioned — placement is the
subsystem with the most history behind it and the most rules to respect.

## Data flow

```
PDH counters ─┐
DXGI adapters ─┼──► Collector::Impl::sample() ──► Snapshot ──► History rings
process table ─┘         (collector thread)        (mutex)      60 s + 60 min
                                                       │
                                  PostMessage(sampleMessage)
                                                       ▼
                                          Application::update()
                                              (UI thread)
                                                       │
                                        ┌──────────────┴──────────────┐
                                        ▼                             ▼
                              Renderer::drawBitmap()          tray tooltip,
                              UpdateLayeredWindow             UIA window text
```

`Snapshot` is a value type. The UI thread copies it out of the collector and
owns its copy for the frame, so there is no shared mutable state during drawing.

Sampling does not depend on window visibility. History keeps accumulating while
the panel is hidden, minimised or fully covered.

## Metrics

| Metric | Source                                                      |
| ------ | ----------------------------------------------------------- |
| CPU    | PDH, per logical processor, inverted from `% Idle Time`      |
| cores  | `GetLogicalProcessorInformationEx`, grouped by efficiency class |
| GPU    | PDH `GPU Engine`, busiest engine per adapter                 |
| VRAM   | PDH `GPU Adapter Memory` against the DXGI dedicated budget   |
| RAM    | `GlobalMemoryStatusEx`, plus per-process private working set |
| apps   | process table grouped by executable identity                 |

Protected processes are skipped. Anything unavailable is `missing` (a quiet NaN)
and renders as an em dash rather than a zero — `valid()` is the only correct way
to test a metric.

## Settings

A plain `key=value` file in `%LOCALAPPDATA%\NativePerfMonitor-1.3\settings.ini`,
guarded by a marker file containing the application id. The loader refuses
redirected paths, reparse points, oversized files and directories owned by
anything else, and clamps every value on read. Saves are written to a temporary
file and moved into place, so an interrupted write cannot corrupt the settings.

Version-specific paths and registry keys mean two major versions can be
installed side by side without touching each other's state.

## What is deliberately not done

- No kernel driver, service or scheduled task
- No code injection into Explorer, and no Explorer UI automation beyond
  **reading** the taskbar layout
- No writes to any shell window
- No elevation; installation is per user
- No network access of any kind

The taskbar strip is drawn in a gap in the taskbar by placing an ordinary
topmost window there. Explorer is never modified or subclassed.
