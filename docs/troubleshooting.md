# Troubleshooting

Start here for any placement problem: choose **Record placement trace** in the
tray menu, reproduce the problem, and choose it again. The tray notification
tells you where `trace.log` was saved. It records every z-order action the
monitor takes and why, with timestamps, which is far more useful in a report
than a description of what it looked like.

## The strip blinks when I click the taskbar

Fixed in 1.5. Clicking the taskbar raises it to the top of the topmost band;
the strip is now *owned* by the taskbar, so the window manager carries it along
in the same operation. If it blinks again, check the trace for
`strip: found below the taskbar` lines — each one is a repair, and there should
be none during ordinary taskbar use.

## The panel or strip disappears and only comes back after toggling it

Fixed in 1.5. The usual cause was **Show Desktop** (Win+D, or the sliver at the
far right of the taskbar): Windows 11 raises the desktop over every application
window and refuses to let an ordinary window above it. The panel now becomes
temporarily topmost while the desktop is raised, and the strip moves back above
the taskbar within a frame or two. The trace shows `desktop: raised` and
`desktop: back in place` for each transition.

If you run an older copy from a Start menu or desktop shortcut, check which one:
each release line installs to its own folder, and a shortcut from an earlier
line launches the earlier build.

## The panel is behind my windows

While locked, the panel deliberately sits in the desktop layer — above the
wallpaper, below every application — like a desktop widget. It refuses to be
brought forward by anything, which is what keeps it stable.

Unlock it (**Lock panel**) to treat it as an ordinary window you can bring to
the front, move and resize.

## The strip jumps between positions in the taskbar

The chosen gap is changing. The strip prefers the slot it already occupies,
ignores drift under 12 px and moves at most every 1.5 s, so this means the
current slot is genuinely being taken — usually by a taskbar item appearing or
growing. The trace shows every move and whether the strip is inside or above
the taskbar.

Turn off **Prefer inside taskbar** to place it directly above the taskbar
instead, which needs no gap search and no accessibility reads at all.

## The strip hides during a game or video

By design. A full-screen application in the foreground hides the strip, and it
returns 700 ms after the application stops being full screen.

## The panel content is hard to read

Opacity affects the background only; text, traces, grids and graph cards are
always opaque. If the whole panel is hard to read against a busy wallpaper,
raise the opacity in **Panel opacity…**.

## The taskbar or Explorer feels sluggish

Two things touch Explorer, both deliberately light:

- **Reading the taskbar layout** walks Explorer's accessibility tree. It runs
  only while both **Taskbar strip** and **Prefer inside taskbar** are on, at most
  every two seconds and normally every eight.
- **The strip's owner relationship** attaches its thread's input queue to the
  taskbar's. That thread only ever places and draws a small bitmap, so it cannot
  hold the taskbar up.

To rule both out, turn off **Taskbar strip**. If the sluggishness persists, it
is not this program.

## Values show as dashes

A dash means the counter is unavailable, which is not the same as zero:

- **GPU or VRAM** — the selected adapter has no matching PDH instances. Pick a
  different adapter under **GPU**.
- **An application row** — the process is protected and cannot be read.
- **Everything, briefly** — the performance counter service is rebuilding. It
  normally recovers within a few samples.

**About / diagnostics…** reports counter status directly.

## The panel stops updating

The header shows `Stale` when the last sample is more than four seconds old, and
`Paused` when monitoring is paused from the tray menu. Sampling continues while
the panel is hidden or covered, so a gap in history means the process was
actually stopped, suspended or asleep.

## Nothing appears at all

- Check the tray icon. **Desktop panel** and **Taskbar strip** are independent
  toggles and either can be off.
- **Reset positions** recovers a surface that was dragged off-screen or onto a
  monitor that no longer exists.
- Only one instance of a release line runs at a time. Launching a second one
  restores the first rather than starting another.

## Tests fail with "no existing monitor"

`WindowTests` refuses to run while a monitor of the same release line is live on
the desktop, so it cannot interfere with a copy you are using. Exit the running
monitor and re-run.
