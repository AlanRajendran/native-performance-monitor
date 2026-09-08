param(
    [string]$BuildDirectory = "$PSScriptRoot\build",
    [string]$OutputDirectory = "$PSScriptRoot\release"
)
$ErrorActionPreference = 'Stop'
$executable = Join-Path $BuildDirectory 'Release\PerfMonitor.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Build Release and run tests first.' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$stagingRoot = Join-Path $BuildDirectory ('package-' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $stagingRoot 'NativePerfMonitor'
New-Item -ItemType Directory -Path $stage,$OutputDirectory -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination $stage
foreach ($name in @('README.md', 'LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $stage
}
foreach ($name in @('Uninstall.ps1', 'Uninstall.cmd', '.nativeperf-package')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "packaging\$name") -Destination $stage
}
$checksums = Get-ChildItem -LiteralPath $stage -File -Force | Sort-Object Name | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name
}
$checksums | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = Join-Path $OutputDirectory 'NativePerfMonitor-1.1.0-win-x64.zip'
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $true)
Write-Host "Portable package: $archive"
