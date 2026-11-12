param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$Output = 'build-verify/release'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$version = [version][Diagnostics.FileVersionInfo]::GetVersionInfo($Executable).FileVersion
if ($version.Revision -ne 0) { throw 'Release executable must use a major.minor.patch.0 version' }
$directory = New-Item -ItemType Directory -Path $Output -Force
$path = Join-Path $directory.FullName ('TaskbarMonitor.' + $version.ToString(3) + '.zip')
if (Test-Path -LiteralPath $path) { throw "Release ZIP already exists: $path" }
$zip = [IO.Compression.ZipFile]::Open($path, [IO.Compression.ZipArchiveMode]::Create)
try {
    # The 1.2.5 updater requires this exact forward-slash path and extracts no other files.
    $entry = $zip.CreateEntry('TaskbarMonitor/TaskbarMonitor.exe', [IO.Compression.CompressionLevel]::Optimal)
    $source = [IO.File]::OpenRead($Executable)
    $destination = $entry.Open()
    try { $source.CopyTo($destination) } finally { $destination.Dispose(); $source.Dispose() }
} finally { $zip.Dispose() }
$zip = [IO.Compression.ZipFile]::OpenRead($path)
try {
    if ($zip.Entries.Count -ne 1 -or $zip.Entries[0].FullName -cne 'TaskbarMonitor/TaskbarMonitor.exe' -or
        $zip.Entries[0].Length -ne (Get-Item -LiteralPath $Executable).Length) { throw 'Invalid updater-compatible ZIP' }
} finally { $zip.Dispose() }
if ((Get-Item -LiteralPath $path).Length -gt 50MB) { throw 'Release exceeds the updater download limit' }
Get-FileHash -LiteralPath $path -Algorithm SHA256
