# Selected B draft generation record

Tool: built-in image_gen (no API CLI).

Intent: edit the selected compact-core-rows draft, preserving its composition and applying the user's added requirements.

Output: B-compact-cores-revised.png. This is an illustrative design draft, not the implemented native UI or a measured alpha-compositing screenshot. The implementation target is 25% background opacity with opaque text and graph lines.

Windows CPU topology was read with GetLogicalProcessorInformationEx and saved in cpu-topology.json. The user-selected variant supersedes the initial A/B/C drawings, which still used binary units and less transparency.

## Final prompt

+Use case: ui-mockup, precise-object-edit. Edit the attached B Compact core rows design draft, preserving the narrow tall one-window layout, font style, restrained native Windows 11 appearance, total CPU graph, fourteen compact horizontal per-core sparklines, three GPU/VRAM/RAM graphs, five-row application table and wallpaper. These are illustrative readings, not a live screenshot.
IMPORTANT CHANGES:
1. MUCH HIGHER TRANSPARENCY. The whole panel background should be only 25% charcoal opacity (75% transparent). Make the blue wallpaper folds clearly visible and continuous through the panel and all chart/table backgrounds, with no opaque inner cards or heavy blur. Text, core badges and graph strokes stay fully opaque, crisp and legible. A very thin panel outline. The present dark opaque panel must become dramatically more see-through. Keep the same wallpaper image and panel silhouette.
2. Heading above the panel becomes "B — Compact cores · Updated". Small caption "DESIGN DRAFT · SAMPLE DATA". Inside title "Performance", status "Live · Locked". CPU model "Intel Core Ultra 5 245KF". Subtitle "6 performance + 8 efficiency cores". Aggregate CPU reading is "15.9%".
3. Replace the ungrouped core rows with TWO groups separated by a subtle short heading. EXACTLY fourteen core rows, not more or fewer. Heading "Performance cores · 6" followed by these SIX rows with labels and values: "Core 0  P" 4%, "Core 1  P" 6%, "Core 10  P" 4%, "Core 11  P" 5%, "Core 12  P" 6%, "Core 13  P" 3%. Next heading "Efficiency cores · 8", followed by these EIGHT rows: "Core 2  E" 9%, "Core 3  E" 84%, "Core 4  E" 7%, "Core 5  E" 5%, "Core 6  E" 8%, "Core 7  E" 6%, "Core 8  E" 68%, "Core 9  E" 7%. Put P/E in tiny outlined badges, explicit group headings written in full. Preserve those Windows core numbers exactly; the performance cores are NOT Core 0 through Core 5. Use cyan/blue for P sparklines, softly brighter turquoise for E sparklines. Two highly active E rows must have high right-hand endpoints matching 84% and 68%; idle rows stay low. All sparklines share a fixed 0-100% vertical scale and 60-second span.
4. ALL MEMORY NUMBERS MUST USE DECIMAL GB or MB, never GiB or MiB, including tiny axis and table labels. GPU label "GPU", value "28%", small hardware text "RTX 5070 Ti" if space. Dedicated VRAM shows "3.65 / 16.77 GB". RAM shows "13.74 / 34.03 GB". Use 0-100% graph axes to avoid cramped capacity text, keep no GiB/MiB anywhere.
5. Five-row table columns "Application", "CPU", "GPU", "VRAM", "RAM", with EXACT rows:
Blender (2) | 7.2% | 22.0% | 2.26 GB | 2.58 GB
Edge (12) | 2.4% | 4.1% | 503 MB | 1.93 GB
VS Code (8) | 1.7% | 0.6% | 147 MB | 1.29 GB
Discord (6) | 0.8% | 1.2% | 101 MB | 440 MB
Spotify (5) | 0.3% | 0.2% | 67 MB | 294 MB
Do not add sorting arrows or extra features. Keep subtle familiar application icons. Footer "60 seconds · Updates every second".
The resulting image should be a complete high-resolution portrait design draft with all fourteen rows and the five application rows comfortably visible, no clipped text. No taskbar strip, taskbar, browser chrome, code or technical implementation notes. Final rendered type labels, decimal units and visibly increased transparency are critical.
