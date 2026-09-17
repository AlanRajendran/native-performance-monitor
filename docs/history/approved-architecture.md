# Native Windows performance monitor — architecture for approval

Status: design proposal, 6 September 2026. The accompanying images contain illustrative data. No monitor has been built or launched, and no Windows configuration has been changed.

The proposed application is one C++20 executable, `PerfMonitor.exe`, with two native windows and one notification-area icon. The first release targets Windows 11 x64, with source structured for a separate native ARM64 build. It runs as the signed-in user. It uses Windows-supplied libraries only at runtime, with the C/C++ runtime statically linked.

The runtime excludes Rainmeter, TrafficMonitor, LibreHardwareMonitor, WinRing0, custom kernel drivers, services, Explorer extensions, browser engines, WebView2, Electron, .NET, and companion monitoring programs. Collection uses the existing Windows counter providers and graphics driver interfaces from user mode. No driver is installed by this application.

## The two surfaces

| Surface | Default size at 100% scaling | Appearance and behavior |
| --- | --- | --- |
| Desktop panel | 420 × 548 device-independent pixels (DIP) | Top-right of the primary monitor's work area, with 24-DIP margins. Four stacked summary graphs, followed by five application rows. Stays behind ordinary application windows. |
| Taskbar strip | 344 × 54 DIP | Bottom-right, 24 DIP from the monitor's work-area edge and 6 DIP above the taskbar. Four distinct 60-second miniature graphs, labeled CPU, GPU, VRAM, and RAM. |

Both surfaces start visible and locked. They have no taskbar button, take no focus while monitoring, and pass pointer input to the desktop or application underneath when locked. The panel's lock indicator is informational; the notification-area menu controls it.

The visual design uses Segoe UI, tabular numbers, thin graph lines, restrained area fills, subtle grid lines, and a small corner radius. CPU is blue, GPU purple, dedicated VRAM teal, and RAM amber, with adjusted contrast in each theme. Every graph has a fixed 0–100% scale. Memory labels additionally show used and total capacity in binary units. All graphs share the same history and timestamps across both surfaces.

`visual-preview.png` shows both themes in the reliable solid fallback. `placement-and-menu.png` shows the panel behind a normal application, the strip above the taskbar, and the notification-area menu. These are static design drawings, not screenshots of a working application.

The strip stays visible above ordinary applications and therefore occupies a small area of their bottom-right content. It reserves no work area and covers no taskbar controls. It hides while a foreground full-screen application occupies the primary monitor, and while an intersecting shell menu or flyout is open. With taskbar auto-hide, it follows the taskbar's visible state and leaves the edge reveal target clear. Detection must be verified against the supported Windows builds.

## Runtime structure

```text
PerfMonitor.exe — one process / one per-session instance
│
├─ UI thread
│  ├─ hidden top-level controller: messages, lifecycle, tray icon/menu
│  ├─ desktop panel HWND
│  ├─ taskbar strip HWND
│  └─ shared Direct2D / DirectWrite resources, layout, theme state
│
└─ collection worker
   ├─ persistent PDH queries and reusable result buffers
   ├─ DXGI adapter identity/capacity cache
   ├─ process identity and timing cache
   ├─ four fixed 60-slot sample rings
   └─ application aggregation → five-row ranking

worker → bounded latest snapshot → posted UI message → repaint
```

There are two application-owned threads; Windows libraries may create internal threads. There is no timer loop in the renderer, second collector for the strip, separate tray process, network client, or telemetry service. A per-session named mutex and small local command channel prevent duplicate monitors and allow the executable to request shutdown for removal.

The controller is a hidden top-level window rather than a message-only window, so it can receive system broadcasts and the `TaskbarCreated` message. Collection and process discovery never run in a paint handler. If a provider stalls, the interface and menu remain responsive and mark readings stale; workers are not repeatedly spawned to work around a blocked provider.

## Collection and metric definitions

Use a coalescible waitable timer with a one-second cadence and monotonic timestamps. Collect system graphs each tick; collect process memory, process CPU deltas, and publish the ranking every second tick. Reuse the two one-second GPU samples for the two-second application interval. Keep all queries open between samples and use `PdhAddEnglishCounterW` for language-neutral counter names. Handle wildcard arrays, changing instances, PDH validity flags, and buffer resizing explicitly. [Microsoft PDH documentation](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhaddenglishcounterw), [formatted counter arrays](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhgetformattedcounterarrayw).

| Reading | Source | Meaning |
| --- | --- | --- |
| System CPU | `\Processor Information(_Total)\% Processor Time` | Busy processor time across the machine, 0–100%. This is not a promise of exact equality with Task Manager's frequency-adjusted CPU utility display. |
| System GPU | `\GPU Engine(*)\Utilization Percentage` | For the selected adapter: sum process contributions to each distinct physical engine, then take the busiest engine. Do not sum independent engines. |
| Dedicated VRAM | `\GPU Adapter Memory(*)\Dedicated Usage` plus DXGI capacity | Adapter-level dedicated bytes used, divided by that adapter's dedicated capacity. Match counter instances to the DXGI adapter LUID and physical node. |
| System RAM | `GlobalMemoryStatusEx` | Total usable physical RAM minus available physical RAM. Show used/total and percentage; this includes system memory, unlike the application table. |
| Application CPU | `GetProcessTimes` deltas over the actual two-second interval | Sum kernel and user CPU time for the executable group, divided by elapsed wall time and all active logical processors. Establish a baseline before reporting a new process. |
| Application GPU | The same GPU-engine counter samples, joined by PID and engine | Sum the group's process contributions per physical engine, average over the ranking interval, then take the busiest engine on the selected adapter. |
| Application VRAM | `\GPU Process Memory(*)\Dedicated Usage` | Sum dedicated memory attributed to processes in the executable group on the selected adapter. |
| Application RAM | `\Process V2(*)\Working Set - Private` and `ID Process`, where available; otherwise legacy `\Process(*)` equivalents | Sum resident private working sets, not virtual address space, private committed bytes, or shared pages counted repeatedly. |

DXGI enumerates adapter names, LUIDs, outputs, software-adapter flags, and dedicated capacity. By default choose the first hardware adapter in high-performance preference order, with a preference for a discrete adapter; otherwise use the primary display adapter. The tray menu allows explicit adapter selection. GPU and VRAM always refer to the same selected adapter in both windows and the table. The monitor placement setting is independent of GPU selection. Linked-adapter/node mappings must be validated; an unresolved mapping is unavailable, never silently combined with another adapter.

The GPU convention is consistent with Microsoft's busiest-engine explanation. Per-process GPU memory can include shared allocations attributed to more than one process, so table sums need not equal adapter usage. WDDM support and available counters determine GPU availability. [Microsoft's GPU measurement explanation](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/).

Do not use `IDXGIAdapter3::QueryVideoMemoryInfo().CurrentUsage` as whole-machine VRAM: that API reports the calling process's usage and budget. [DXGI documentation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo).

An integrated GPU without dedicated capacity shows `N/A · Shared GPU` in the VRAM surface. It does not rename shared RAM to VRAM. Missing or denied counters show an em dash and a short unavailable reason in diagnostics. A new or invalid utilization sample produces a gap, not a zero. After sleep, a device reset, or a long collection gap, reset rate baselines and preserve gaps on the 60-second timeline. A change of selected adapter clears GPU and VRAM history. Pausing is explicit and also leaves a timestamp gap on resume.

Microsoft documents a Windows 10 issue in per-process GPU memory counters. Its applicability to the eventual Windows 11 test machine must be checked rather than assumed; the adapter graph remains independent of summed process memory. Invalid or implausible provider data is flagged, not corrected by fabricated estimates. [Microsoft's counter issue advisory](https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/gpu-process-memory-counters-report-wrong-value).

## Application grouping and pressure ranking

Process identity is `(PID, creation time)`, not a PDH instance suffix. Discover candidate PIDs from process counters, scope the default list to the interactive session, and resolve executable paths with `QueryFullProcessImageNameW` using limited query rights. Cache identity, process times, display name, and a small icon; refresh metadata only for new identities or changed executables. Joining a legacy memory counter to a PID is valid only within that collection snapshot. Reset CPU baselines on PID reuse or process restart.

Group every process with the same normalized full executable path into one row, irrespective of its parent PID. The row shows a friendly name and process count, such as `Edge (12)`. Executables with the same basename in different locations remain distinct; different helper executables remain distinct. If the full path cannot be obtained, do not guess a group from the name: keep the process separate and identify the incomplete metadata. Exited processes are removed without retaining their resource use in the next ranking.

Exclude the idle/system pseudo-processes and core Windows infrastructure. The initial path-qualified list covers `smss`, `csrss`, `wininit`, `services`, `lsass`, `winlogon`, `svchost`, `fontdrvhost`, `dwm`, `conhost`, `explorer`, `RuntimeBroker`, `SearchIndexer`, `SearchHost`, `StartMenuExperienceHost`, `ShellExperienceHost`, and `ApplicationFrameHost` at their expected Windows locations. Do not hide every Microsoft executable or everything beneath the Windows directory: user-facing applications such as Notepad and Calculator remain eligible. No signature scan runs on the sampling timer. Exclusions affect only the table; system graphs include the entire system.

For a group, calculate four comparable percentages:

```text
c = its CPU percentage of total machine capacity
g = its GPU percentage on the selected adapter
v = 100 × attributed dedicated bytes / dedicated capacity
r = 100 × private resident bytes / usable physical RAM

pressure = max(c, g, v, r), over valid available dimensions
```

This is a transparent ranking heuristic, not a measurement of stalls or memory eviction. It intentionally allows an application consuming substantial memory to outrank a modest CPU user. Missing dimensions are omitted, never treated as measured zero; a row with no valid dimensions is unranked. Shared GPU-memory attribution may make `v` exceed 100%, which is retained as attributed data rather than silently clamped. Show the five highest scores, using the sum of valid percentages and then executable path as deterministic tie breakers. Do not add a fifth numeric pressure column. CPU, GPU, VRAM, and RAM values remain directly visible. Fewer than five eligible applications leave empty rows instead of filling the list with Windows processes.

## Window placement, input, DPI, and theme

Use native tool windows with no activation during monitoring. The desktop panel requests the bottom of the ordinary window order using `SetWindowPos(HWND_BOTTOM, …, SWP_NOACTIVATE)`. Out-of-context WinEvent notifications trigger a debounced check when foreground windows, desktop state, or shell geometry change; only this application's windows are repositioned. `WINEVENT_OUTOFCONTEXT` keeps callbacks in this process. [Window ordering](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos), [WinEvent delivery](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook).

The panel is an independent window above the visible desktop background, not a child injected into Explorer's icon view. No `WorkerW` reparenting or undocumented `Progman` messages are planned. Normal-window occlusion is a requirement; behavior during Win+D/Show desktop, virtual-desktop switching, and Explorer restart is a prototype acceptance check. Windows can hide independent windows during Show desktop; permanent visibility through every shell transition is not promised. The tray's Desktop panel command restores it if needed.

The strip is an independent, nonactivating topmost tool window whose visibility follows the policy above. Read primary taskbar bounds through `SHAppBarMessage(ABM_GETTASKBARPOS)` and the monitor work area through `GetMonitorInfo`. Observe taskbar movement/show/hide using out-of-context events and recheck geometry at a bounded rate if events are missing. Do not register a reserving appbar, resize the taskbar, inject Explorer code, or alter its settings. [Taskbar geometry API](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shappbarmessage).

**Locked input and Mica:** use `WS_EX_LAYERED | WS_EX_TRANSPARENT` for reliable pointer pass-through across processes, with `WS_EX_NOACTIVATE`. Returning `HTTRANSPARENT` alone is insufficient as a cross-process strategy. The locked drawing path uses Direct2D into a reusable premultiplied 32-bit surface and publishes it through the layered-window API. The fill remains visually solid and high contrast. [Microsoft's layered-window hit-testing documentation](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features).

**Mica where supported:** request `DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW` on a compatible ordinary DWM-composited window, with `DwmExtendFrameIntoClientArea` and Direct2D/DirectWrite content. The documented backdrop attribute starts at Windows 11 build 22621. Mica must be validated with the actual window style, activation state, and OS build; do not promise it on a layered click-through surface. If the locked style cannot retain the system backdrop, use the solid appearance in the preview. An unlocked compatible surface can use Mica. Windows can also choose a solid appearance for inactive windows, accessibility settings, or power-related conditions. No custom blur or wallpaper capture is required. [DWM backdrop types](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type), [Mica behavior](https://learn.microsoft.com/en-us/windows/apps/design/style/mica).

Detect application light/dark changes with the OS `UISettings` color APIs and `ColorValuesChanged`, marshaling the notification to the UI thread, plus `WM_THEMECHANGED`/`WM_SETTINGCHANGE` handling. This is a native Windows API, without WinUI or a managed runtime. If Windows uses separate app and shell color modes, the strip uses the shell mode from the read-only personalization setting, with the app mode as fallback. Update the complete palette, text, DWM attributes, and menu together. High contrast overrides decorative colors and uses system colors. The native tray menu can use owner-drawn entries with standard menu navigation and accessibility; do not depend on undocumented dark-mode ordinals. [Microsoft's Win32 theme guidance](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes).

Declare Per-Monitor V2 DPI awareness in the manifest. Lay out in DIP, handle `WM_DPICHANGED`, and recreate only size-dependent rendering resources. Re-clamp positions on display, work-area, or primary-monitor changes; account for negative desktop coordinates. Restrict dragging to the primary monitor. Standard and compact layouts retain all four graphs and all five table columns. The compact target is 360 × 460 DIP with shorter graphs and abbreviated units; below that available area, keep readable text and provide an unlocked panel scroll view rather than shrinking text indefinitely. UI Automation exposes current metrics and table cells without live announcements every second. [Microsoft's DPI guidance](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows).

Move / resize temporarily unlocks the selected surface and brings it forward for positioning. Locking returns the panel behind applications and the strip to its normal position policy. Reset positions always restores visible primary-monitor defaults. All operations remain accessible through the single notification-area menu shown in the preview.

## Performance budget and verification

The requested idle overhead is a release target, not a measured result at this design stage: average CPU below 0.5% of total machine CPU capacity and approximately 50 MB or less total working set. Also report single-core-equivalent CPU time, private working set, private bytes, and peak working set so the results are interpretable.

Keep four fixed 60-slot rings, reusable PDH buffers, bounded metadata/icon caches, two latest snapshots, and shared Direct2D/DirectWrite resources. Use a top-five selection rather than sorting every process. Skip repainting fully hidden/occluded surfaces while retaining one-second collection. Redraw on sample arrival or a genuine layout/theme change, not at 60 FPS. Do not call `timeBeginPeriod`, poll WMI, spawn PowerShell for counters, or trim working sets to manufacture a low reading. Expire dead-process metadata and avoid unbounded diagnostic logs.

Plan an approximate working-set allocation of 5–10 MB for application state and caches, 4–12 MB for surfaces depending on DPI, and 15–25 MB for code, PDH, fonts, and graphics-library residency, with the rest as headroom. These allocations are estimates; graphics drivers and counter providers can exceed them. Buffer growth is bounded, and exceeding a resource cap produces an explicit incomplete-data state instead of silently dropping contributors. Optimize measured costs without relaxing the specified one-/two-second cadence.

After approval, build automated tests and run them in these groups:

| Test group | Required coverage |
| --- | --- |
| Pure metric tests | CPU normalization including more than 64 logical processors; irregular intervals; 60-second rollover and gaps; PID reuse; same-name different-path processes; missing dimensions; deterministic top five. |
| GPU fixture tests | Sum-then-max engine aggregation; distinct engines and physical nodes; two-second averaging; LUID joins; adapter change; zero dedicated capacity; malformed, missing, and transient counter instances. |
| Lifecycle and storage | Device loss, suspend/resume, stale snapshots, bounded caches, duplicate-launch handling, settings round trip, and removal limited to app-owned paths and its exact autostart entry. |
| Native integration | Single running monitor process, no helper or driver; new/exiting applications; non-English counter names; unavailable permissions; tray recovery after Explorer restart. |
| Visual and input checks | Light/dark/high contrast; Mica and solid paths; cross-process click-through using a test target; unlock/drag/relock; primary-only clamping; 100%, 125%, 150%, and 200% DPI. |
| Desktop behavior | 1366×768, 1920×1080, 2560×1440, and 3840×2160; mixed DPI and negative coordinates; taskbar auto-hide; full-screen apps; Show desktop and virtual desktops. |
| Performance | Optimized release build, two-minute warm-up and ten-minute measured idle runs with both surfaces visible, representative process counts, at least 100% and 200% DPI; a longer stability run for memory/handle growth. |

Tests use a separate test target only during development. The portable runtime does not include a resident test helper. Compare readings with the corresponding Windows counters and clearly distinguish expected Task Manager differences. Record hardware, Windows build, GPU driver, resolution, sample counts, average CPU, working-set percentiles, and peak memory in the delivered results. Report which hardware/OS matrix entries were actually tested; do not claim unrun combinations passed.

## Settings, autostart, removal, and delivery

On first approved application launch, store only application-owned settings under `%LOCALAPPDATA%\NativePerfMonitor`: visibility, lock state, primary-relative positions, size, selected adapter, and schema version. Save atomically when a setting changes, not every sample. There is no persistent metrics database or process-history log by default.

Start with Windows is **off by default**. Enabling it adds one quoted command to `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\NativePerfMonitor`. It requires no administrator privileges, service, scheduled task, or machine-wide registry changes. Read the actual registration when showing menu state; do not automatically re-enable a setting the user or Windows disabled. Validate the supported Run-command length before writing it. [Microsoft's per-user Run-key documentation](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys).

After preview approval, deliver source code and build instructions, automated tests, actual validation results, and a portable ZIP containing the executable, readme, license notices, checksums, and a complete user-invoked uninstaller. The executable also exposes Uninstall in its tray menu. Removal stops the monitor, removes its exact optional Run entry and application-owned settings/logs, and deletes the package's known files, including the executable and removal scripts. Use a short-lived removal script because a running Windows executable cannot normally delete its own mapped image; it is not a background monitoring dependency. Resolve and validate every deletion target, refuse reparse-point escapes, leave unrelated files in a shared extraction directory, and remove the directory only if empty. The uninstaller does not attempt to erase Windows-managed execution history or unrelated downloaded archives/source folders.

Preview approval authorizes the implementation and deliverables. It does not enable autostart or install the application into Windows. Any build/SDK tools that turn out to be missing will be reported before installing them.

The design is ready for approval with three explicit behavior choices: the panel is an independent window behind applications; the strip floats just above the taskbar; and locked surfaces use the solid fallback when Mica cannot coexist with reliable click-through. CPU/memory goals and the native shell behavior remain implementation acceptance checks.
