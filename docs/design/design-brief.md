# Native Performance Monitor 1.2 design proposal

Status: user selected B, compact core rows, then requested explicit core-type labels, decimal GB/MB and stronger translucency. The updated visual remains a design proposal; the 1.1 application and Windows configuration have not been changed. GitHub work is deferred.

## Two priorities

1. Show individual CPU cores in the large translucent desktop panel. Remove the small taskbar surface from this version. Keep GPU, dedicated VRAM, RAM and the five grouped application rows.
2. Put the actual application and a clearly named native uninstall application directly in the portable package root. Neither executable should require browsing build, Release, bin, or nested product directories.

The current machine reports Intel Core Ultra 5 245KF, 14 physical cores and 14 logical processors, verified with a read-only Win32_Processor query on 2026-09-10. The drawings use illustrative readings, not a live measurement.

## Drafts

| Draft | Layout | Proposed default size, DIP | Tradeoff |
|---|---|---|---|
| A: Balanced grid | Total CPU, fourteen core graphs in two rows, three GPU/VRAM/RAM graphs, then top applications | 620 x 800 | All resources visible together with little added height |
| B: Compact core rows, selected | Total CPU, fourteen labeled full-width sparklines grouped by core type, remaining resource graphs and application table | 450 x 880 | Narrow desktop footprint, easier row-by-row core comparison |
| C: Wide split | CPU total/core grid/application table on the left, three resource graphs on the right | 880 x 630 | Larger graphs on a wide display, greater horizontal footprint |

All three layouts follow Windows light/dark mode automatically. A and B illustrate dark mode; C illustrates light mode. Background opacity is reduced from version 1.1's 62% to a proposed 25% (75% transparency), including table and chart surfaces: nested cards must not stack opaque fills. The wallpaper should be clearly visible through the panel. Text and graph strokes remain opaque for readability. High contrast retains an opaque accessibility fallback. An opacity control in the tray menu will allow adjustment without changing Windows appearance settings.

## Decimal memory units

All memory displays use decimal GB and MB, with exact case. GB is bytes divided by 1,000,000,000; MB is bytes divided by 1,000,000. This applies to RAM, dedicated VRAM, per-application columns, summaries, axis labels, diagnostics and accessible descriptions. It is a numerical conversion, not a label replacement: 1 GiB is 1.073741824 GB, and 512 MiB is 536.870912 MB. Keep raw counters in bytes and convert only at the display boundary. Percentages continue to use raw used/capacity values. Illustrative 3.4 GiB becomes 3.65 GB, and 12.8 GiB becomes 13.74 GB. Actual device totals must come from Windows, not nominal product labels. Add tests at decimal-unit boundaries and for consistent precision and rounding.

Every CPU-core chart has a fixed 0-100% range and 60-second history. Core labels remain in stable topology order instead of jumping with load. The current percent sits beside each label. CPU-total history remains available to distinguish a busy single core from high overall utilization. GPU, dedicated VRAM and RAM retain their histories. The application table retains Application, CPU, GPU, VRAM and RAM columns and the existing grouping/exclusion rules.

## Desktop and background behavior

The large panel remains on the primary desktop behind ordinary application windows. Collection continues once per second while covered, unfocused, or hidden through the tray menu. Ranking continues every two seconds. Repainting can stop while fully occluded; collection and the rolling history do not depend on repainting. Resume after system sleep inserts gaps instead of invented zero readings.

Locking makes the panel click-through. Unlocking enables moving, resizing and scrolling when needed. A single notification-area icon controls show/hide, locking, layouts, pause, optional autostart and exit. The taskbar widget, its placement menu items and its taskbar-control polling thread are removed from this version. No Explorer injection, service, driver or additional monitoring process is introduced.

## Core collection

Discover physical-core and logical-processor relationships once with GetLogicalProcessorInformationEx. Batch per-logical-processor Windows performance counters into the existing one-second collector. Keep bounded 60-sample histories and publish one coherent snapshot to the UI; do not create one thread or timer per core.

The detected 14-core/14-thread machine has one logical processor per physical core, so each row directly represents one core. On SMT hardware, label physical cores and expose sibling-thread detail explicitly; a physical-core summary is the mean utilization of its logical siblings, not an invented hardware execution-capacity reading. Preserve processor-group identifiers and omit aggregate counter instances. Missing or first samples display as unavailable.

Read-only GetLogicalProcessorInformationEx results on this machine expose two efficiency classes: class 1 for Windows CPUs 0, 1, 10, 11, 12, 13; class 0 for CPUs 2 through 9. Microsoft documents higher classes as higher-performance, lower-efficiency cores. Together with Intel's confirmed 6P + 8E specification, the selected design labels these groups as follows. Do not assume the first six Windows CPU numbers are all P-cores.

| Group | Visible row labels in order |
|---|---|
| Performance cores, 6 | Core 0 P, Core 1 P, Core 10 P, Core 11 P, Core 12 P, Core 13 P |
| Efficiency cores, 8 | Core 2 E, Core 3 E, Core 4 E, Core 5 E, Core 6 E, Core 7 E, Core 8 E, Core 9 E |

Keep the group headings written out; use short P/E badges on rows. Type must remain understandable without color. A subtle blue/cyan variation may distinguish the groups. The 245KF does not need an additional low-power-E group. On other devices, only name extra types when Windows/processor information supports that name; otherwise use an explicit class label rather than guessing LP-E, P or E. On homogeneous CPUs, omit unnecessary type distinctions.

Topology evidence is in cpu-topology.json. References: [Microsoft PROCESSOR_RELATIONSHIP](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-processor_relationship) and [Intel Core Ultra 5 245KF specifications](https://www.intel.com/content/www/us/en/products/sku/241066/intel-core-ultra-5-processor-245kf-24m-cache-up-to-5-20-ghz/specifications.html).

The planned implementation uses documented [Windows processor topology](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformationex) and [PDH sampling](https://learn.microsoft.com/en-us/windows/win32/perfctrs/collecting-performance-data) APIs in user mode.

## Portable folder

The ZIP will contain these files directly, without a nested NativePerfMonitor directory:

```text
NativePerfMonitor-1.2.0/       <- user's chosen extraction folder
    PerfMonitor.exe          <- launch the monitor
    Uninstall.exe            <- remove the monitor
    ReadMe.txt
    LICENSE.txt
    package-manifest.json
    SHA256SUMS.txt
```

The two EXEs have distinct icons and file descriptions. Both use normal file attributes, never Hidden or System. They remain easy to distinguish even if Explorer hides filename extensions; the application will not change that Windows preference. Source, tests and build output are delivered separately from the ready-to-run folder. A ready-extracted folder will also accompany the local ZIP delivery so the executable is immediately accessible.

Uninstall.exe will be an actual Windows executable with a simple confirmation dialog, not a renamed command script. It runs only when requested. It must stop only the verified monitor from this folder, remove its matching optional per-user startup entry, remove owned settings and package files, and remove both EXEs. Cleanup must preserve unrelated files, reject redirected paths and report any incomplete removal accurately. Any temporary cleanup worker must exist only during uninstall; no persistent service, scheduled task, reboot requirement or administrator privilege is planned. Complete self-removal and temporary-file cleanup must be verified before delivery.

The tray uninstall action invokes the same visible uninstaller. Autostart remains optional and off by default for a fresh package. No Windows theme, taskbar or security preference is modified.

## Checks required before delivery

- Core-to-thread mapping, processor groups, missing samples, startup warm-up, hotplug/topology refresh, and fixed-length histories.
- Core readings and all histories continue changing while a normal window fully covers the panel and while the panel is hidden.
- Correct DPI layout, text clipping, high core counts, the 25% opacity baseline without stacked opaque cards, lock/unlock hit testing, and absence of a taskbar-strip window.
- Verify decimal conversions numerically across every memory display, including accessible labels and diagnostics; no GiB/MiB labels remain in the runtime UI.
- Extract the final ZIP and verify both real EXEs are directly in its root, visible and runnable without administrator privileges.
- Native uninstaller confirmation/cancel, running-app removal, matching startup cleanup, self-removal, temporary-worker cleanup, unrelated-file preservation, and path/ownership refusal cases.
- Re-measure the new binary's idle overhead with per-core collection enabled. The target remains below 0.5% average whole-machine CPU and around 50 MB working set. Version 1.1 measurements are not results for this proposed version.

B is the selected layout. The revised illustration incorporates the user's further unit, transparency and core-type requirements. The illustrations show layout and visual intent; the final native renderer will determine exact pixel spacing and adaptive sizing. Building a new version and replacing the user's current executable are separate from this design-only delivery.
