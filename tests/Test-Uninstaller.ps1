param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$TestRoot
)
$ErrorActionPreference = 'Stop'
$source = Split-Path -Parent $PSScriptRoot
$identity = 'NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2-v2.0'
$TestRoot = [IO.Path]::GetFullPath($TestRoot)
New-Item -ItemType Directory -Path $TestRoot -Force | Out-Null
$assertions = 0
function Assert([bool]$Condition, [string]$Message) {
    $script:assertions++
    if (-not $Condition) { throw $Message }
}
function New-Fixture([string]$Name) {
    $folder = Join-Path $TestRoot ($Name + '-' + [Guid]::NewGuid().ToString('N'))
    $package = Join-Path $folder "portable's files & spaces"
    $data = Join-Path $folder 'isolated settings'
    New-Item -ItemType Directory -Path $package,$data -Force | Out-Null
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $package 'PerfMonitor.exe')
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $Executable) 'Uninstall.exe') -Destination (Join-Path $package 'Uninstall.exe')
    @{ owner=$identity; version='1.3.0' } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'package-manifest.json')
    Set-Content -LiteralPath (Join-Path $package 'ReadMe.txt') -Value 'fixture readme'
    Set-Content -LiteralPath (Join-Path $data '.nativeperf-settings') -Value $identity -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $data 'settings.ini') -Value 'panel=1'
    return @{ Package=$package; Data=$data; Folder=$folder }
}
function Invoke-Removal($Fixture) {
    $resultFile = Join-Path $Fixture.Folder 'result.json'
    $p = Start-Process -FilePath (Join-Path $Fixture.Package 'Uninstall.exe') -ArgumentList @('--isolated','--yes','--data-dir',('"'+$Fixture.Data+'"'),'--result',('"'+$resultFile+'"')) -WindowStyle Hidden -PassThru
    if (-not $p.WaitForExit(10000)) { throw 'Uninstaller launcher did not exit.' }
    $p.Dispose()
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while (-not (Test-Path -LiteralPath $resultFile) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path -LiteralPath $resultFile)) { throw 'Uninstaller did not produce a completion report.' }
    $report = Get-Content -LiteralPath $resultFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($report.success) { return ,@(0) }
    Write-Output $report.message | Out-File -LiteralPath (Join-Path $Fixture.Folder 'failure.log')
    return ,@(1)
}

$fixture = New-Fixture 'complete'
$result = Invoke-Removal $fixture
Assert ($result[-1] -eq 0) 'Complete removal failed.'
Assert (-not (Test-Path -LiteralPath $fixture.Package)) 'Empty package directory was not removed.'
Assert (-not (Test-Path -LiteralPath $fixture.Data)) 'Empty settings directory was not removed.'
Assert (-not (Test-Path -LiteralPath (Join-Path $fixture.Package 'Uninstall.exe'))) 'Native uninstall EXE remained.'

$fixture = New-Fixture 'preserve'
Set-Content -LiteralPath (Join-Path $fixture.Package 'keep.txt') -Value 'unrelated package file'
Set-Content -LiteralPath (Join-Path $fixture.Data 'keep.txt') -Value 'unrelated settings file'
$result = Invoke-Removal $fixture
Assert ($result[-1] -eq 0) 'Removal from shared directories failed.'
Assert (Test-Path -LiteralPath (Join-Path $fixture.Package 'keep.txt')) 'Unrelated package file was removed.'
Assert (Test-Path -LiteralPath (Join-Path $fixture.Data 'keep.txt')) 'Unrelated settings file was removed.'
Assert (-not (Test-Path -LiteralPath (Join-Path $fixture.Package 'Uninstall.exe'))) 'Uninstaller did not remove itself.'

$fixture = New-Fixture 'invalid-marker'
Set-Content -LiteralPath (Join-Path $fixture.Package 'package-manifest.json') -Value '{"owner":"different owner"}'
$result = Invoke-Removal $fixture
Assert ($result[-1] -ne 0) 'Invalid ownership marker was accepted.'
Assert (Test-Path -LiteralPath (Join-Path $fixture.Package 'PerfMonitor.exe')) 'Unowned executable was removed.'
Assert (Test-Path -LiteralPath (Join-Path $fixture.Data 'settings.ini')) 'Settings changed after failed validation.'

$fixture = New-Fixture 'redirected'
$link = Join-Path $fixture.Folder 'redirected-package'
New-Item -ItemType Junction -Path $link -Target $fixture.Package | Out-Null
$redirected = @{ Package=$link; Data=$fixture.Data; Folder=$fixture.Folder }
$result = Invoke-Removal $redirected
Assert ($result[-1] -ne 0) 'Reparse-point package was accepted.'
Assert (Test-Path -LiteralPath (Join-Path $fixture.Package 'PerfMonitor.exe')) 'Junction target was altered.'

$fixture = New-Fixture 'running'
$fixtureProcess = Start-Process -FilePath (Join-Path $fixture.Package 'PerfMonitor.exe') `
    -ArgumentList @('--data-dir', ('"' + $fixture.Data + '"')) -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Milliseconds 2000
    Assert (-not $fixtureProcess.HasExited) 'The uninstall fixture did not stay running. Close other monitor instances first.'
    $result = Invoke-Removal $fixture
    Assert ($result[-1] -eq 0) 'Removal of a running package failed.'
    Assert ($fixtureProcess.WaitForExit(5000)) 'The package process remained after uninstall.'
    Assert (-not (Test-Path -LiteralPath $fixture.Package)) 'The running package was not fully removed.'
    Assert (-not (Test-Path -LiteralPath $fixture.Data)) 'Running-package settings were not removed.'
}
finally {
    if (-not $fixtureProcess.HasExited) { $fixtureProcess.Kill(); $fixtureProcess.WaitForExit() }
    $fixtureProcess.Dispose()
}

Write-Output "$assertions uninstaller assertions passed. Autostart registry was not modified."
