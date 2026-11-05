param(
    [string]$Executable,
    [int]$AppProcessId,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

function ConvertTo-ReleaseVersion([string]$Tag) {
    if ($Tag -notmatch '^v?(\d+)\.(\d+)\.(\d+)$') { throw 'Invalid release version' }
    return [version]($Tag.TrimStart('v') + '.0')
}

function Get-UpdateAsset($Release) {
    $version = ConvertTo-ReleaseVersion $Release.tag_name
    if ($Release.draft -or $Release.prerelease) { throw 'Not a stable release' }
    $name = 'TaskbarMonitor.' + $version.ToString(3) + '.zip'
    $assets = @($Release.assets | Where-Object { $_.name -ceq $name })
    if ($assets.Count -ne 1) { throw 'Expected one release ZIP' }
    $asset = $assets[0]
    $expectedUrl = 'https://github.com/kuy124/TaskbarMonitor/releases/download/' + $Release.tag_name + '/' + $name
    if ($asset.browser_download_url -cne $expectedUrl) { throw 'Unexpected download URL' }
    if ($asset.digest -cnotmatch '^sha256:[a-f0-9]{64}$') { throw 'Release has no SHA-256 digest' }
    if ($asset.size -le 0 -or $asset.size -gt 50MB) { throw 'Unexpected download size' }
    return $asset
}

function Expand-Update([string]$ZipPath, [string]$Destination, [version]$Version) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        # Only extract the known executable, never arbitrary archive paths.
        $entries = @($zip.Entries | Where-Object { $_.FullName -ceq 'TaskbarMonitor/TaskbarMonitor.exe' })
        if ($entries.Count -ne 1 -or $entries[0].Length -le 0 -or $entries[0].Length -gt 50MB) {
            throw 'Invalid release archive'
        }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $Destination, $false)
    } finally { $zip.Dispose() }
    $actual = [Diagnostics.FileVersionInfo]::GetVersionInfo($Destination).FileVersion
    if ([version]$actual -ne $Version) { throw 'Executable version does not match release' }
}

function Test-UpdatesEnabled {
    $settings = Get-ItemProperty -LiteralPath 'HKCU:\Software\TaskbarMonitor' -ErrorAction SilentlyContinue
    return $null -eq $settings -or $null -eq $settings.AutoUpdate -or $settings.AutoUpdate -ne 0
}

if ($SelfTest) {
    if ((ConvertTo-ReleaseVersion 'v1.2.10') -le (ConvertTo-ReleaseVersion '1.2.9')) { throw 'Version comparison failed' }
    foreach ($tag in @('1.2', '1.2.3-beta', '../1.2.3', '1.2.3/evil')) {
        $rejected = $false
        try { ConvertTo-ReleaseVersion $tag | Out-Null } catch { $rejected = $true }
        if (!$rejected) { throw "Accepted unsafe tag: $tag" }
    }
    $release = [pscustomobject]@{
        tag_name = '1.2.5'; draft = $false; prerelease = $false
        assets = @([pscustomobject]@{
            name = 'TaskbarMonitor.1.2.5.zip'; size = 123
            digest = 'sha256:' + ('a' * 64)
            browser_download_url = 'https://github.com/kuy124/TaskbarMonitor/releases/download/1.2.5/TaskbarMonitor.1.2.5.zip'
        })
    }
    Get-UpdateAsset $release | Out-Null
    foreach ($field in @('digest', 'browser_download_url', 'name', 'size')) {
        $original = $release.assets[0].$field
        $release.assets[0].$field = 'invalid'
        $rejected = $false
        try { Get-UpdateAsset $release | Out-Null } catch { $rejected = $true }
        if (!$rejected) { throw "Accepted unsafe asset: $field" }
        $release.assets[0].$field = $original
    }
    Write-Output 'Updater validation passed'
    return
}

$stage = $null
$lock = $null
$closed = $false
$backup = $null
$replaced = $false
$accepted = $false
try {
    if (!(Test-UpdatesEnabled)) { return }
    $app = Get-Process -Id $AppProcessId -ErrorAction Stop
    if ($app.Path -ine $Executable) { return }
    $current = [version][Diagnostics.FileVersionInfo]::GetVersionInfo($Executable).FileVersion
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    # Public releases API: no GitHub token, credentials, or private repository access.
    $release = Invoke-RestMethod 'https://api.github.com/repos/kuy124/TaskbarMonitor/releases/latest' -Headers @{'User-Agent'='TaskbarMonitor-Updater'; 'Accept'='application/vnd.github+json'} -TimeoutSec 30
    $version = ConvertTo-ReleaseVersion $release.tag_name
    if ($version -le $current) { return }
    $asset = Get-UpdateAsset $release
    if (!(Test-UpdatesEnabled) -or $app.HasExited) { return }
    Add-Type -AssemblyName System.Windows.Forms
    $answer = [Windows.Forms.MessageBox]::Show("TaskbarMonitor $($version.ToString(3)) is available. Download, install, and restart now?", 'TaskbarMonitor update', 'YesNo', 'Information')
    if ($answer -ne 'Yes' -or !(Test-UpdatesEnabled) -or $app.HasExited) { return }
    $accepted = $true

    # Stage beside the executable so replacement is an atomic same-volume operation.
    $directory = [IO.Path]::GetDirectoryName($Executable)
    $lock = [IO.File]::Open((Join-Path $directory '.taskbarmonitor-update.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    $stage = Join-Path $directory ('.taskbarmonitor-update-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    $archive = Join-Path $stage 'update.zip'
    Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $archive -TimeoutSec 120
    if ((Get-Item -LiteralPath $archive).Length -ne $asset.size) { throw 'Download size mismatch' }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ine $asset.digest.Substring(7)) { throw 'Download checksum mismatch' }
    $replacement = Join-Path $stage 'TaskbarMonitor.exe'
    Expand-Update $archive $replacement $version
    if (!(Test-UpdatesEnabled) -or $app.HasExited) { return }
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class UpdateWindow {
    public delegate bool Callback(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
    [UpdateWindow]::EnumWindows({ param($window, $unused)
        $ownerId = 0
        [UpdateWindow]::GetWindowThreadProcessId($window, [ref]$ownerId) | Out-Null
        $name = New-Object Text.StringBuilder 256
        [UpdateWindow]::GetClassName($window, $name, 256) | Out-Null
        if ($ownerId -eq $AppProcessId -and $name.ToString() -eq 'TaskbarMonitorHost') {
            [UpdateWindow]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    if (!$app.WaitForExit(15000)) { throw 'App did not close; update cancelled' }
    $closed = $true
    $backup = Join-Path $stage 'previous.exe'
    [IO.File]::Replace($replacement, $Executable, $backup)
    $replaced = $true
    $restarted = Start-Process -FilePath $Executable -ArgumentList '--autostart' -PassThru
    Start-Sleep -Seconds 3
    if ($restarted.HasExited) { throw 'Updated app did not stay running' }
    $closed = $false
} catch {
    $failure = $_.Exception.Message
    if ($closed) {
        if ($replaced -and (Test-Path -LiteralPath $backup)) {
            try { [IO.File]::Replace($backup, $Executable, $null) } catch {
                # Keep the backup if restoration fails; never delete the last working copy.
                $stage = $null
            }
        }
        Start-Process -FilePath $Executable -ArgumentList '--autostart' -ErrorAction SilentlyContinue
    }
    # Offline, API rate limits, and unwritable folders must not disrupt the monitor.
    if ($accepted) {
        [Windows.Forms.MessageBox]::Show("Update could not be installed: $failure`n`nYou can download the release manually from GitHub.", 'TaskbarMonitor update', 'OK', 'Warning') | Out-Null
    }
} finally {
    if ($stage) { Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue }
    if ($lock) { $lock.Dispose() }
    Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
}
