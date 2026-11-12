param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null

$version = '0.9.6'
$expectedHash = '086d9f1b5a99e643edc2cfaaac16051685b551e4c5ac0b32a57c58c0e529c001'
$archive = Join-Path $output 'LibreHardwareMonitor.zip'
$url = "https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases/download/v$version/LibreHardwareMonitor.zip"
if (!(Test-Path -LiteralPath $archive)) {
    $download = $archive + '.download'
    try {
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $download
        if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
            throw 'LibreHardwareMonitor release checksum mismatch'
        }
        Move-Item -LiteralPath $download -Destination $archive
    } finally { Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
    throw 'Cached LibreHardwareMonitor release checksum mismatch'
}

# Only the .NET Framework library and its dependency closure, not the standalone application.
$libraries = @(
    'LibreHardwareMonitorLib.dll', 'System.Memory.dll', 'RAMSPDToolkit-NDD.dll',
    'HidSharp.dll', 'DiskInfoToolkit.dll', 'System.Runtime.CompilerServices.Unsafe.dll',
    'BlackSharp.Core.dll', 'System.Numerics.Vectors.dll', 'System.Buffers.dll'
)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($name in $libraries) {
        $entries = @($zip.Entries | Where-Object { $_.FullName -ceq $name })
        if ($entries.Count -ne 1 -or $entries[0].Length -le 0 -or $entries[0].Length -gt 20MB) {
            throw "Invalid bundled library: $name"
        }
        $path = Join-Path $output $name
        $input = $entries[0].Open()
        try {
            $file = [IO.File]::Create($path)
            try { $input.CopyTo($file) } finally { $file.Dispose() }
        } finally { $input.Dispose() }
    }
} finally { $zip.Dispose() }

$compiler = Join-Path ([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()) 'csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework C# compiler is required' }
$bridge = Join-Path $output 'TaskbarMonitor.Sensors.dll'
& $compiler /nologo /optimize+ /target:library "/out:$bridge" "/reference:$(Join-Path $output 'LibreHardwareMonitorLib.dll')" (Join-Path $root 'src\SensorBridge.cs')
if ($LASTEXITCODE -ne 0) { throw 'Could not compile the embedded sensor bridge' }

$source = "https://raw.githubusercontent.com/LibreHardwareMonitor/LibreHardwareMonitor/v$version"
$notice = @"
TaskbarMonitor bundles LibreHardwareMonitorLib $version and its library dependencies.
LibreHardwareMonitor is Copyright LibreHardwareMonitor and Contributors, and
Michael Moller / OpenHardwareMonitor contributors. It is licensed under MPL 2.0.
The bundled upstream libraries are unmodified.

Source code: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/v$version
Source archive: https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/archive/refs/tags/v$version.zip
TaskbarMonitor's sensor bridge: src/SensorBridge.cs (MIT, like TaskbarMonitor).

"@
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.txt')) {
    $path = Join-Path $output ('Upstream-' + $name)
    if (!(Test-Path -LiteralPath $path)) {
        Invoke-WebRequest -UseBasicParsing -Uri "$source/$name" -OutFile $path
    }
    $notice += "`r`n`r`n" + [IO.File]::ReadAllText($path)
}
[IO.File]::WriteAllText((Join-Path $output 'ThirdParty-Licenses.txt'), $notice, [Text.UTF8Encoding]::new($false))

$resources = @('#pragma code_page(65001)')
$names = @('STRINGTABLE', 'BEGIN')
$resourceId = 2500
foreach ($name in ($libraries + @('TaskbarMonitor.Sensors.dll', 'ThirdParty-Licenses.txt'))) {
    $path = (Join-Path $output $name).Replace('\', '\\').Replace('"', '\"')
    $resources += "$resourceId RCDATA `"$path`""
    $names += "    $resourceId `"$name`""
    ++$resourceId
}
$names += 'END'
[IO.File]::WriteAllLines((Join-Path $output 'EmbeddedSensors.rc'), ($resources + $names), [Text.UTF8Encoding]::new($false))
