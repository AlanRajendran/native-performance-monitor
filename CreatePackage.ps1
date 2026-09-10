param(
    [string]$BuildDirectory = "$PSScriptRoot\build",
    [string]$OutputDirectory = "$PSScriptRoot\release",
    [string]$PortableDirectory
)
$ErrorActionPreference = 'Stop'
$executable = Join-Path $BuildDirectory 'Release\PerfMonitor.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Build Release and run tests first.' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$uninstaller = Join-Path $BuildDirectory 'Release\Uninstall.exe'
if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) { throw 'Build Uninstall.exe first.' }
if (-not $PortableDirectory) { $PortableDirectory = Join-Path $OutputDirectory 'NativePerfMonitor-1.2.0' }
$stage = [IO.Path]::GetFullPath($PortableDirectory)
if (Test-Path -LiteralPath $stage) { throw "Portable destination already exists: $stage" }
New-Item -ItemType Directory -Path $stage,$OutputDirectory -Force | Out-Null
Copy-Item -LiteralPath $executable -Destination $stage
Copy-Item -LiteralPath $uninstaller -Destination $stage
foreach ($name in @('LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $stage
}
foreach ($name in @('ReadMe.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "packaging\$name") -Destination $stage
}
@{ owner='NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2-v1.2'; version='1.2.0' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') -Encoding UTF8
foreach ($file in Get-ChildItem -LiteralPath $stage -File -Force) {
    $file.Attributes = [IO.FileAttributes]::Normal
}
$checksums = Get-ChildItem -LiteralPath $stage -File -Force | Sort-Object Name | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name
}
$checksums | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding ASCII
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = Join-Path $OutputDirectory 'NativePerfMonitor-1.2.0-win-x64.zip'
if (Test-Path -LiteralPath $archive) { throw "Archive already exists: $archive" }
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
Write-Host "Ready-to-run folder: $stage"
Write-Host "Portable package: $archive"
