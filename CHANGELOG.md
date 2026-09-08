# Changes

## 1.1.0

- Place the four-graph strip inside unused taskbar space by default, with an option to return above it.
- Detect taskbar control bounds through read-only, cached UI Automation on a background thread.
- Use translucent surfaces in both lock states while keeping text and graph traces readable.
- Preserve click-through while locked and native dragging/resizing while unlocked.
- Add tests for taskbar gap selection, control avoidance, and premultiplied alpha.
- Fix PNG exports to convert premultiplied pixels into straight alpha correctly.

## 1.0.0

Initial native Windows x64 release with one-second graphs, two-second grouped process ranking, optional per-user startup, portable packaging, and guarded uninstall.
