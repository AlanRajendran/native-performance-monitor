[CmdletBinding()]
param(
    [switch]$Yes,
    [string]$DataDirectory,
    [switch]$Isolated
)

$ErrorActionPreference = 'Stop'
$ownerId = 'NativePerfMonitor-6D845648-584B-48CE-9904-03E95B0B69E2'
$packageDirectory = [IO.Path]::GetFullPath($PSScriptRoot)
$defaultData = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NativePerfMonitor'
if (-not $DataDirectory) { $DataDirectory = $defaultData }
$DataDirectory = [IO.Path]::GetFullPath($DataDirectory)

function Assert-NoReparsePoint([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a redirected path: $current"
            }
        }
        $parent = [IO.Directory]::GetParent($current)
        if ($null -eq $parent -or $parent.FullName -eq $current) { break }
        $current = $parent.FullName
    }
}

function Assert-OwnedFile([string]$Directory, [string]$Name) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $Directory $Name))
    $prefix = $Directory.TrimEnd('\') + '\'
    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'A deletion target escaped the application directory.'
    }
    Assert-NoReparsePoint $candidate
    if ((Test-Path -LiteralPath $candidate) -and -not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Expected an application file, found a directory: $candidate"
    }
    return $candidate
}

try {
    Assert-NoReparsePoint $packageDirectory
    Assert-NoReparsePoint $DataDirectory
    if (-not $Isolated -and -not $DataDirectory.Equals($defaultData, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'A custom settings directory requires the explicit -Isolated option.'
    }
    $marker = Assert-OwnedFile $packageDirectory '.nativeperf-package'
    if (-not (Test-Path -LiteralPath $marker) -or
        (Get-Content -LiteralPath $marker -Raw).Trim() -ne $ownerId) {
        throw 'The portable package ownership marker is missing or invalid. Nothing was removed.'
    }

    # Validate every prospective file before stopping a process or changing anything.
    $packageNames = @('PerfMonitor.exe', 'README.md', 'LICENSE.txt', 'SHA256SUMS.txt',
        'Uninstall.cmd', 'Uninstall.ps1', '.nativeperf-package')
    $packageFiles = @($packageNames | ForEach-Object { Assert-OwnedFile $packageDirectory $_ })
    $dataNames = @('settings.ini', 'settings.ini.new', 'diagnostics.txt', '.nativeperf-settings')
    $dataFiles = @()
    if (Test-Path -LiteralPath $DataDirectory) {
        $dataMarker = Assert-OwnedFile $DataDirectory '.nativeperf-settings'
        if (-not (Test-Path -LiteralPath $dataMarker) -or
            (Get-Content -LiteralPath $dataMarker -Raw).Trim() -ne $ownerId) {
            throw 'The settings ownership marker is missing or invalid. Nothing was removed.'
        }
        $dataFiles = @($dataNames | ForEach-Object { Assert-OwnedFile $DataDirectory $_ })
    }

    $executable = Join-Path $packageDirectory 'PerfMonitor.exe'
    if (Test-Path -LiteralPath $executable) {
        $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
        if ($version.OriginalFilename -ne 'PerfMonitor.exe' -or $version.ProductName -ne 'NativePerfMonitor') {
            throw 'The executable does not identify itself as NativePerfMonitor. Nothing was removed.'
        }
    }
    if (-not $Yes) {
        Write-Host "Remove Performance monitor from $packageDirectory and its application settings?"
        if ((Read-Host 'Type YES to remove it').Trim() -cne 'YES') { exit 0 }
    }

    $running = @(Get-Process -Name PerfMonitor -ErrorAction SilentlyContinue | Where-Object {
        try { $_.Path -and $_.Path.Equals($executable, [StringComparison]::OrdinalIgnoreCase) }
        catch { $false }
    })
    if ($running.Count -gt 0) {
        & $executable --exit
        foreach ($process in $running) {
            if (-not $process.WaitForExit(15000)) {
                # The user requested removal. Only the verified package process is stopped.
                $stillRunning = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
                if ($stillRunning -and $stillRunning.Path -eq $executable) {
                    Stop-Process -Id $process.Id -Force
                    $stillRunning.WaitForExit(5000) | Out-Null
                }
            }
        }
    }

    if (-not $Isolated) {
        $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
            'Software\Microsoft\Windows\CurrentVersion\Run', $true)
        if ($key) {
            try {
                $expected = '"' + $executable + '" --autostart'
                $actual = [string]$key.GetValue('NativePerfMonitor', '')
                if ($actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
                    $key.DeleteValue('NativePerfMonitor', $false)
                }
            }
            finally { $key.Dispose() }
        }
    }

    # Exact files only. No recursive deletion, wildcard removal, or shell-built commands.
    foreach ($file in $dataFiles) {
        Assert-NoReparsePoint $file
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    }
    foreach ($file in $packageFiles) {
        Assert-NoReparsePoint $file
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
    }
    foreach ($directory in @($DataDirectory, $packageDirectory) | Select-Object -Unique) {
        Assert-NoReparsePoint $directory
        if ((Test-Path -LiteralPath $directory) -and
            @(Get-ChildItem -LiteralPath $directory -Force).Count -eq 0) {
            Remove-Item -LiteralPath $directory -Force
        }
    }
    Write-Host 'Performance monitor removed. Unrelated files were kept.'
    exit 0
}
catch {
    Write-Error $_
    exit 1
}
