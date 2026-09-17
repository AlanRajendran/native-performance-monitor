param([string]$Configuration = 'Release', [string]$BuildDirectory = "$PSScriptRoot\build")
$ErrorActionPreference = 'Stop'
cmake -S $PSScriptRoot -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
& "$PSScriptRoot\tests\Test-Uninstaller.ps1" -Executable "$BuildDirectory\$Configuration\PerfMonitor.exe" -TestRoot "$BuildDirectory\uninstall-tests"
