# Window placement

Placement is where every stability problem in this program has lived. 1.3 and
1.4 both tried to keep the surfaces in place by *watching and repairing*: poll
the z-order, notice a surface in the wrong place, put it back. That design
cannot work on Windows 11, and 1.5 replaces it. This document explains why, and
the rules that keep it working.

## What was actually going wrong

Measured on Windows 11 25H2 (build 26200) with a 10 ms z-order recorder while
reproducing the reported triggers:

| Trigger | 1.4 | 1.5 |
| --- | --- | --- |
| Clicking or keyboard-navigating the taskbar | strip covered **246 ms**, every time | **0 ms** |
| Opening an ordinary window | no effect | no effect |
| Show Desktop (Win+D, the taskbar corner) | panel hidden **for the whole duration**; strip lost **permanently** | panel visible after **~49 ms**; strip after **~31 ms** |
| Leaving Show Desktop | — | both back in place within **~30 ms** |

Two separate mechanisms were responsible.

**The taskbar covers anything that is merely topmost.** Whenever the taskbar is
activated, Explorer raises it to the top of the topmost band. A strip that is
just another topmost window ends up underneath and can only be put back after
the fact. 1.4 did that after its 250 ms debounce — a quarter-second blink on
every taskbar click.

**Show Desktop cannot be repaired with an ordinary window.** Windows 11 does not
minimize windows for Show Desktop; it raises the desktop host (`Progman`) above
every application window. While it is raised, `SetWindowPos(panel, HWND_TOP)`
**returns success and changes nothing** — verified directly. Every repair 1.3
and 1.4 made in that state "succeeded" and left the panel buried. Only a topmost
window can be above the raised desktop.

## The design

### The strip is owned by the taskbar

`StripHost` creates the strip with `Shell_TrayWnd` as its **owner**. The window
manager keeps an owned window above its owner as part of the same operation that
moves the owner, so the taskbar cannot rise over the strip even for a frame.
From the `SetWindowPos` documentation: *"Any window … owned by a topmost window
is itself made a topmost window, to ensure that all owned windows stay above
their owner."*

That guarantee has one hole. A caller can reposition a window with
`SWP_NOOWNERZORDER`, which skips the owner adjustment, and Explorer's Show
Desktop path does exactly that: afterwards the taskbar sits above the strip and
nothing moves it back. So the strip also runs `keepAboveTaskbar()`: on every
foreground change, every 250 ms, and on every frame, it walks up the z-order from
itself; if the taskbar is above it, it moves directly above the taskbar. In
normal use the walk finds nothing and costs nothing.

### The strip has its own thread

Ownership across processes **attaches the input queues** of the two threads
(Raymond Chen, [*Is it legal to have a cross-process parent/child or owner/owned
window relationship?*][chen-cross]). If the thread that owns the strip ever
stopped pumping messages while input was pending for it, input to the taskbar
would stall with it.

So the strip lives on a dedicated thread that does nothing but place and draw a
344×42 bitmap. Panel rendering, menus, dialogs and counter queries all run
elsewhere and can never delay the taskbar. The strip thread reads no application
state: the UI thread hands it a complete `StripFrame` (rectangle, visibility,
palette, the few values it draws) under a mutex, and it posts back only a
handful of messages (dragged, menu requested, closed).

Explorer's taskbar and desktop run on **different threads** (checked: 9476 and
9068 on the test machine). If one of our threads were owned by both, it would
transitively couple Explorer's taskbar and desktop threads to each other. That
is one reason the panel is *not* owned by `Progman`, even though that would also
work: it uses the technique below instead, which couples to nothing.

### The panel lives in the desktop layer

The locked panel belongs just above the wallpaper and below every application.

1. It is placed with `HWND_BOTTOM`. Because the shell window is special, this
   lands directly above `Progman`.
2. Its `WM_WINDOWPOSCHANGING` sets `SWP_NOZORDER` on every z-order change it did
   not make itself — activation, `ShowWindow`, another program's
   `SetWindowPos`. Its own placement passes `SWP_NOSENDCHANGING` and never sees
   the veto. With this in place nothing in normal use can move it, so there is
   nothing to repair.
3. **Show Desktop** is detected with a hidden probe window kept at
   `HWND_BOTTOM`. Normally the probe sits just above the desktop host; while the
   host is raised, the probe is below it. `FindWindowEx(nullptr, host, probe)`
   therefore answers the question in one call.
4. While the desktop is raised, the panel becomes topmost and is inserted just
   below the lowest other topmost window — the bottom of the topmost band — so
   it covers the raised desktop and nothing else. When the desktop drops back,
   `HWND_BOTTOM` clears topmost and returns it to the desktop layer in a single
   call, never passing over application windows.

The check runs on every foreground change (Show Desktop begins and ends with
one), three more times over the following 120 ms because Explorer raises the
desktop a few milliseconds around the event, and every 250 ms as a backstop.

Steps 2–4 are the technique [Rainmeter][rainmeter-system] uses for its "On
Desktop" skins, including its handling of the desktop host moving under
`Progman` in Windows 11 24H2. The probe was verified on 25H2 before relying on
it: normal → Show Desktop → restored read as normal → raised → normal.

## What may be cached, and what may not

**Position and size may be cached.** Nothing else moves these windows. A pass
that computes the same rectangle calls nothing.

**Z-order may not be cached.** Any process can restack any window at any time
without telling us. 1.4 briefly compared against a remembered anchor, which
suppressed the very repair that was needed. In 1.5 z-order is either protected
(the panel's veto), carried (the strip's owner), or observed at the moment of
use (`keepAboveTaskbar`, the probe). It is never remembered.

## Things not to do

- **Do not re-add a periodic z-order re-assertion.** That is what made 1.3 fight
  Explorer. Observe, then act only when what you can see is wrong.
- **Do not widen the WinEvent hooks.** Foreground and minimize are all placement
  needs. The 1.3 range delivered focus, selection, state and location events from
  every window in every process.
- **Do not move work onto the strip thread.** Its only job is to never block.
- **Do not own the panel by `Progman` from the same thread as the strip.** See
  the transitive attachment above.

## Event sources

| Source | Cadence | Effect |
| --- | --- | --- |
| `EVENT_SYSTEM_FOREGROUND` | on focus change | desktop check now and 3× over 120 ms; strip verify; geometry pass after 250 ms |
| `EVENT_SYSTEM_MINIMIZESTART` / `END` | on minimize | geometry pass after 250 ms |
| desktop timer | 250 ms | desktop check, strip verify |
| reconcile timer | 1 s | geometry pass, visibility recovery |
| `TaskbarObserver` | 2–8 s | strip slot recalculated |
| `TaskbarCreated` | Explorer restart | desktop state re-read; the strip rebuilds under the new taskbar on its next frame |

## The strip's slot inside the taskbar

`taskbarSlot()` returns the free gap nearest a preferred x. The preferred x is
the slot the strip **already occupies**, so a taskbar change that does not touch
the current slot returns the same answer. `settleStrip()` then ignores drift
under 12 px and moves at most once every 1.5 s. Without this, an app opening, a
badge appearing or the weather widget reflowing would each move the strip.

| Constant | Value | Protects against |
| --- | --- | --- |
| `stripSettleMs` | 1500 | the strip hopping between taskbar gaps |
| `stripDeadband` | 12 px | sub-icon drift in the taskbar layout |
| `stripRestoreMs` | 700 | the strip returning too eagerly after a full-screen app |
| `geometryDelayMs` | 250 | bursts of shell events |
| `desktopIntervalMs` | 250 | Show Desktop poll, as Rainmeter uses |
| `TaskbarObserver::idleIntervalMs` | 8000 | cost of walking Explorer's tree |
| `TaskbarObserver::minimumIntervalMs` | 2000 | the same, while shell events keep arriving |
| `TaskbarObserver::tolerance` | 6 px | a label reflowing by a pixel |

The strip is hidden in only two cases: a full-screen application in the
foreground, and an auto-hidden taskbar that has slid away. It used to hide for
menus and shell flyouts too, because a re-asserted topmost strip could end up
above them. An owned strip sits directly above the taskbar, so anything the shell
opens later lands above it naturally.

## Diagnosing a report

Choose **Record placement trace** in the tray menu (or start with `--trace`),
reproduce the problem, and choose it again. `trace.log` in the settings folder
then records every placement action with a timestamp and a thread id: Show
Desktop transitions, panel layer changes, strip repairs and rebuilds, and every
strip move with the reason. It is off otherwise and costs nothing.

[chen-cross]: https://devblogs.microsoft.com/oldnewthing/20130412-00/?p=4683
[rainmeter-system]: https://github.com/rainmeter/rainmeter/blob/master/Library/System.cpp
