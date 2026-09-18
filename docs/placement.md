# Window placement

Placement is where every stability problem in this program has lived. This
document explains the rules, because they are not obvious from the code and
because breaking them is how the flickering came back the last two times.

## The one rule

**Placement is edge triggered.** A pass computes what the surfaces *should*
look like, compares that against what the shell was *last told*, and calls into
the window manager only for the parts that differ.

The 1.3.0 code was level triggered: once a second, and on every accepted shell
event, it recomputed the geometry and reapplied all of it unconditionally. That
works only while every input is perfectly stable. In practice several inputs are
derived from a live scan of the desktop, and any one of them oscillating turned
directly into visible movement, because nothing in the chain compared anything.

If you add a new input to placement, assume it will oscillate on somebody's
machine, and damp it.

## What a pass does

`Application::layout()` runs one pass:

1. Read the monitor work area and DPI.
2. Compute the panel rectangle from the settings, clamped to the work area.
3. `applyGeometry(panel, rect)` — compares and moves only if needed.
4. `restack()` — repairs the panel's z-order, rate limited (see below).
5. Compute the strip rectangle, either inside the taskbar or above it.
6. `settleStrip()` — damps the horizontal position (see below).
7. `applyGeometry(strip, rect)`, then `keepStripAbove()` — re-asserts topmost
   only when the taskbar is observed to be above the strip.
8. `visibility()` — show or hide each surface.

A pass is cheap when nothing changed, which is the normal case. That is the
point: it is safe to run often precisely because it usually does nothing.

## What may be cached, and what may not

This is the distinction the whole subsystem turns on.

**Position and size may be cached.** Nothing else moves these windows. What was
last passed to `SetWindowPos` is still true, so comparing against it is sound.

**Z-order may not be cached.** Any process can restack any window at any time
without telling us. A remembered anchor says what we last *asked for*, which is
not evidence of where the window actually sits now. Z-order has to be observed
on each pass and acted on when it is wrong.

Getting this wrong is not theoretical: 1.4.0 briefly cached the z-order anchor
alongside the rectangle, and the result was that the surfaces would sink behind
the wallpaper host or the taskbar and never come back, because the repair that
would have fixed it was suppressed by a comparison against a stale anchor. The
symptom was surfaces vanishing until the user toggled them off and on in the
tray menu — toggling worked only because `ShowWindow` raises a window within its
band as a side effect.

Each `Surface` therefore records:

| Field          | Meaning                                       |
| -------------- | --------------------------------------------- |
| `applied`      | rectangle passed to the last `SetWindowPos`   |
| `appliedValid` | false until the first successful placement    |
| `appliedText`  | last string written with `SetWindowTextW`     |

`applyGeometry()` compares against `applied` and returns without calling
anything when the rectangle still matches. `applyZOrder()` has no cache and
always calls the shell; its callers are responsible for having just observed
that the window is in the wrong place:

- **Panel** — `desktopAnchor()` walks the z-order and returns the panel's own
  handle when it is already sitting correctly, so a stable desktop costs one
  enumeration and no shell call.
- **Strip** — `stripBelowTaskbar()` walks upwards from the strip and reports
  whether `Shell_TrayWnd` is above it, which is the only way it can be hidden.

Both are rate limited by `restackIntervalMs`, which exists solely to bound how
hard this pushes back when another program is restacking us repeatedly. It does
not delay the first repair.

A layered window keeps its old bitmap across a resize, so `applyGeometry()`
repaints when the extent changed. Without that the new area shows stale pixels
until something else happens to repaint.

## Invalidation

The position cache is dropped by `invalidatePlacement()` when the world
genuinely changed:

- Explorer restarted (`TaskbarCreated`) — the new taskbar sits above the strip
  and the desktop host is a different window
- `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`, `WM_DPICHANGED`
- lock state changed (`applyMode()`), which changes window styles and z-order band
- the user issued a command, or finished dragging a surface

1.3.0 instead ran an unconditional per-second
`SetWindowPos(strip, HWND_TOPMOST, …)`, re-inserting the strip at the top of the
topmost band on top of `Shell_TrayWnd` forever. Explorer re-asserts the
taskbar's position on various shell events, and the old event hook accepted
`Shell_TrayWnd` events, so the two could drive each other. Whether the loop
closed depended on the Windows build and taskbar configuration, which is exactly
the shape of a bug that hits some machines and not others.

**Do not re-assert z-order on a timer, and do not skip the repair on a cached
answer.** Observe, then act only when what you can see is wrong. That is the
only formulation that is both stable and self-healing.

## The damping constants

All in `Application`, all deliberately generous:

| Constant             | Value  | Protects against                                      |
| -------------------- | ------ | ----------------------------------------------------- |
| `restackIntervalMs`  | 600    | another program and this one restacking in a loop     |
| `stripSettleMs`      | 1500   | the strip hopping between taskbar gaps                |
| `stripRestoreMs`     | 700    | the strip blinking as a window passes over it         |
| `stripDeadband`      | 12 px  | sub-icon drift in the taskbar layout                  |
| `geometryDelayMs`    | 250    | bursts of shell events                                |

And in `TaskbarObserver`:

| Constant            | Value  | Protects against                            |
| ------------------- | ------ | ------------------------------------------- |
| `idleIntervalMs`    | 8000   | cost of walking Explorer's tree             |
| `minimumIntervalMs` | 2000   | the same, when shell events keep arriving   |
| `tolerance`         | 6 px   | a clock or weather label reflowing by a pixel |

Lowering the strip and event constants makes the surfaces more responsive and
less stable; that trade has already been made once in the wrong direction.
`restackIntervalMs` is different in kind: it does not delay a repair, it only
caps how often one can be repeated, so it is safe to keep short.

## Strip stickiness

`taskbarSlot()` returns the free gap nearest a preferred x. The preferred x is
the slot the strip **already occupies**, not the user's configured position, so
a taskbar change that does not touch the current slot returns the same answer.

On top of that, `settleStrip()` keeps the current position when the drift is
under `stripDeadband`, or when the last move was less than `stripSettleMs` ago.

Before this, any change to the taskbar's contents re-picked the nearest gap. An
app opening, a notification badge appearing, or the weather widget's text
changing width would each move the strip. On a machine with widgets enabled that
is close to continuous — which is why the strip twitched on some PCs and sat
still on others.

## Hiding the strip

`hideStrip()` decides whether the strip should be suppressed: a full-screen
foreground app, an auto-hidden taskbar, or a shell menu overlapping it.

`stripSuppressed()` wraps it asymmetrically:

- **Hide immediately.** A menu must never be covered, and hiding on its own
  never looks like flicker.
- **Restore only after `stripRestoreMs` of quiet.** A blink requires a hide
  followed promptly by a show, so delaying the show is what actually removes it.

## Event sources

| Source                                    | Cadence          | Handler              |
| ----------------------------------------- | ---------------- | -------------------- |
| `EVENT_SYSTEM_FOREGROUND`                 | on focus change  | debounced pass       |
| `EVENT_OBJECT_SHOW` / `HIDE`              | filtered by class| debounced pass       |
| `EVENT_SYSTEM_MINIMIZESTART` / `MINIMIZEEND` | on minimize   | debounced pass       |
| `WM_TIMER` id 1                           | 1 s              | reconciliation pass  |
| `TaskbarObserver`                         | 2–8 s            | pass + strip repaint |
| `TaskbarCreated`                          | Explorer restart | invalidate + pass    |

The hook previously covered `EVENT_OBJECT_SHOW` through
`EVENT_OBJECT_LOCATIONCHANGE` — ten event types including focus, all four
selection events, state change and location change, from every window in every
process. `OBJID_WINDOW` filtering removed the caret and control-level noise, but
window-level location changes still arrive continuously while anything is being
dragged, resized or animated. Each burst scheduled a pass, and each pass could
repaint the whole panel in software.

**Do not widen the hook range.** If you need to notice something new, prefer a
specific event, and verify the cost while dragging a window around on a slow
machine.

## Why the once-a-second pass still exists

It is the backstop. Anything the hooks do not observe — a window moving without
emitting events, another program restacking the surfaces, a state the
reconciliation logic did not anticipate — is repaired within a second. Because
the pass is edge triggered it costs almost nothing when there is nothing to do.
