# Version 1.2 architecture

This document supersedes the historical two-surface architecture for version 1.2.

## Runtime

One PerfMonitor.exe process owns a hidden notification controller and one visible, layered desktop panel. A timed user-mode collector supplies immutable snapshots to rendering. The panel stays on the primary monitor behind ordinary windows and becomes click-through while locked. Hiding or covering the panel does not stop collection. No taskbar strip window or taskbar-observer thread is created.

The collector samples Windows PDH counters once per second and updates grouped process ranking every second sample. DXGI identifies adapters and dedicated memory capacity; Win32 supplies physical memory and executable identity. Process handles and metadata are cached with bounded refresh. No kernel access, driver, service, network collector or Explorer modification is used.

## Physical cores

GetLogicalProcessorInformationEx(RelationProcessorCore) provides processor-group masks and efficiency classes. Each physical core maps to one or more (processor group, logical index) keys. The batched Processor Information(*) / % Idle Time counter supplies logical idle fractions; utilization is 100 minus idle, bounded to 0..100 only after PDH reports a valid nonnegative finite value. This avoids inverse busy-time counter anomalies near idle. Invalid status still remains unavailable. All siblings must be valid before their arithmetic mean becomes a physical-core sample.

Each core has a fixed 60-sample history. Two efficiency classes receive P/E group labels; one class is generic; additional classes keep their Windows class numbers. Topology refreshes infrequently, preserving history if unchanged. The CPU brand is read once using CPUID. No ordering assumption assigns core types.

Microsoft references:
- https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformationex
- https://learn.microsoft.com/en-us/troubleshoot/windows-server/performance/troubleshoot-performance-problems-in-windows

## Rendering and interaction

Direct2D/DirectWrite draw into premultiplied alpha pixels composed by Windows. This deliberate translucent treatment replaces the earlier Mica-oriented design to satisfy the later stronger-transparency request. Background alpha defaults to 25%, configurable to 15% or 40%; text and traces stay legible. Theme changes follow Windows app light/dark preferences. High contrast uses system colors and a solid surface.

Default size is 450 x 880 DIP. Compact size is 420 x 720 DIP. Content can scroll while unlocked on shorter screens or CPUs with more cores. Per-monitor DPI awareness and primary-work-area constraints preserve usability across display changes.

## Packaging and ownership

The ZIP root has six files: PerfMonitor.exe, Uninstall.exe, ReadMe.txt, LICENSE.txt, package-manifest.json and SHA256SUMS.txt. Both EXEs are visible normal files. Static CRT avoids redistributable installation.

Settings, single-instance identity, window classes and optional per-user startup registration are version-isolated from 1.1. Uninstall.exe is a native confirmation launcher with an embedded, transient Windows PowerShell cleanup payload. The payload runs only after confirmation so it can remove both binaries, settings and matching startup entry. It checks package identity, executable metadata and path redirection, stops only a process from the exact package, deletes exact owned filenames and removes only empty directories. User files and earlier versions are preserved.
