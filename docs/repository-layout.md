# Repository layout

```
.
├── CMakeLists.txt            build definition for every target
├── README.md                 what it is and how to run it
├── CHANGELOG.md              user-visible changes per release
├── src/                      the application
├── tests/                    unit and integration tests, driven by ctest
├── resources/                icons, manifests, version resources
├── packaging/                files shipped inside the portable ZIP
├── scripts/                  Build.ps1, CreatePackage.ps1
├── docs/                     documentation (this directory)
│   ├── architecture.md       processes, threads, data flow
│   ├── placement.md          window placement rules — read before touching it
│   ├── rendering.md          drawing pipeline, transparency, history ranges
│   ├── troubleshooting.md    symptoms and what causes them
│   ├── images/               figures used by the README
│   ├── design/               design brief and fixtures
│   └── history/              per-version architecture and validation notes
├── build/                    CMake output (ignored)
├── release/                  the current packaged build (ignored)
└── archive/                  earlier builds, zips, benchmarks, screenshots;
                              kept locally, never uploaded (ignored)
```

## Source files

| File                        | Responsibility                                      |
| --------------------------- | --------------------------------------------------- |
| `src/app.cpp`               | controller, panel, desktop layer, placement decisions, tray menu, entry |
| `src/striphost.{h,cpp}`     | the taskbar strip on its own thread, owned by the taskbar |
| `src/accessibility.h`       | read-only UI Automation provider shared by both surfaces |
| `src/trace.{h,cpp}`         | optional placement trace (`--trace`, tray menu)      |
| `src/collector.{h,cpp}`     | PDH/DXGI sampling thread and the `Snapshot` it makes |
| `src/core.{h,cpp}`          | pure logic: history rings, ranking, geometry, formatting |
| `src/cpu.{h,cpp}`           | CPU topology and name via CPUID                      |
| `src/render.{h,cpp}`        | Direct2D drawing, palettes, the DIB surface          |
| `src/settings.{h,cpp}`      | settings file, startup registration, path guards     |
| `src/taskbar.{h,cpp}`       | UI Automation reads of the taskbar layout            |
| `src/install.{h,cpp}`       | per-user installation used by Setup.exe              |
| `src/setup.cpp`             | Setup.exe entry point                                |
| `src/uninstall.cpp`         | Uninstall.exe launcher                               |

`core.cpp` has no Windows UI dependencies, which is why `CoreTests` can cover it
without a desktop. Keep logic there when it is testable in isolation.

## Targets

| Target        | Kind    | Notes                                           |
| ------------- | ------- | ----------------------------------------------- |
| `perf_core`   | static  | pure logic                                      |
| `perf_native` | static  | everything that needs Windows                   |
| `PerfMonitor` | exe     | the monitor                                     |
| `Setup`       | exe     | per-user installer                              |
| `Uninstall`   | exe     | uninstall launcher                              |
| `CoreTests`   | exe     | pure logic; runs anywhere                       |
| `NativeTests` | exe     | rendering, settings, live counters              |
| `WindowTests` | exe     | real windows; needs an interactive desktop      |
| `InstallTests`| exe     | real shortcuts and registry, in a private namespace |

## Building

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Requires MSVC and the Windows 11 SDK. The build is `/W4` and currently warning
clean; keep it that way.

`WindowTests` refuses to run while a monitor instance of the same release line is already on the desktop,
so it does not interfere with a copy the user is actually using. Close the
running monitor before a full test run. It also reports 77 (skip) when there is
no interactive Explorer shell, which is how it behaves in a sandbox.

## Previews

```
build\Release\PerfMonitor.exe --render-preview "<output directory>"
```

Writes both themes, both surfaces, both history ranges and three opacity
settings as PNGs. Useful for reviewing a visual change without running the app,
and for regenerating `artifacts/screenshots/`.
