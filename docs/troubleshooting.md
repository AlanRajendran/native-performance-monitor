# Troubleshooting

## The strip flickers or blinks

Fixed in 1.4.0. If it returns, the cause is almost always that something made
the strip's *visibility* decision oscillate, or made its *position* oscillate.

- Position: `settleStrip()` and the sticky slot preference in `layout()`
- Visibility: `stripSuppressed()`, which restores only after a quiet period

Check **History → 60 seconds** is not being confused with the symptom: switching
ranges repaints once, which is not a flicker.

## The strip jumps between positions in the taskbar

The chosen gap is changing. The strip prefers the slot it already occupies and
ignores drift under 12 px, so this means the current slot is genuinely being
taken — usually by a taskbar item appearing or growing.

Turn off **Prefer inside taskbar** in the tray menu to place it directly above
the taskbar instead, which needs no gap search and no accessibility reads at all.

## The panel disappears behind other windows

While locked, the panel deliberately sits just above the desktop, so ordinary
windows cover it. That is the intended behaviour for a desktop widget.

The z-order repair runs at most every two seconds and is **skipped entirely
while a full-screen application owns the foreground** — reordering windows
underneath an exclusive full-screen game can drop it out of its presentation
mode. So a panel that stays buried while a game is running is working as
designed.

Unlock the panel (**Lock panel**) to bring it to the normal window layer.

## The panel content is hard to read

Opacity affects the background only. If text or graphs look washed out, that is
a bug — see [rendering.md](rendering.md) for the rule. Everything except the
rounded background rectangle is drawn fully opaque.

If the whole panel is hard to read against a busy wallpaper, raise the opacity
in **Panel opacity…**; the graphs sit on opaque cards and stay legible at any
setting.

## The taskbar or Explorer feels sluggish

Reading the taskbar layout means walking Explorer's accessibility tree across a
process boundary, which drives Explorer's own UI thread. The monitor does this
only while **Taskbar strip** *and* **Prefer inside taskbar** are both on, at most
every two seconds and normally every eight.

To rule it out completely, turn off **Prefer inside taskbar**. If the sluggishness
persists, it is not this.

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

Resume from sleep resets the counters deliberately: PDH deltas across a suspend
are meaningless.

## Nothing appears at all

- Check the tray icon. **Desktop panel** and **Taskbar strip** are independent
  toggles and either can be off.
- A full-screen application suppresses the strip by design.
- **Reset positions** recovers a surface that was dragged off-screen or onto a
  monitor that no longer exists.
- Only one instance runs at a time. Launching a second one restores the first
  rather than starting another.

## Tests fail with "no existing monitor"

`WindowTests` refuses to run while a monitor is live on the desktop, so it cannot
interfere with a copy you are using. Exit the running monitor and re-run.
