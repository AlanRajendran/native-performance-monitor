# Configures, builds and tests the repository.
# This script lives in scripts/, so repository paths resolve from its parent.
param([string]$Configuration = 'Release', [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repositoryRoot 'build' }
cmake -S $repositoryRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed. Window tests also fail when a monitor instance is already running; exit it and retry.' }
& (Join-Path $repositoryRoot 'tests\Test-Uninstaller.ps1') -Executable (Join-Path $BuildDirectory "$Configuration\PerfMonitor.exe") -TestRoot (Join-Path $BuildDirectory 'uninstall-tests')
