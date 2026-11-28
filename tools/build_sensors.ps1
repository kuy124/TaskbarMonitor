param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null

function GetPinnedAsset($name, $url, $sha256) {
    $path = Join-Path $output $name
    if (!(Test-Path -LiteralPath $path)) {
        $download = "$path.$([Guid]::NewGuid()).download"
        try {
            Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $download
            if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $sha256) {
                throw "Checksum mismatch: $name"
            }
            Move-Item -LiteralPath $download -Destination $path
        } finally { Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue }
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $sha256) {
        throw "Checksum mismatch: $path"
    }
    return $path
}

$version = '0.9.6'
$lhm = 'https://github.com/LibreHardwareMonitor/LibreHardwareMonitor'
$archive = GetPinnedAsset 'LibreHardwareMonitor.zip' "$lhm/releases/download/v$version/LibreHardwareMonitor.zip" `
    '086d9f1b5a99e643edc2cfaaac16051685b551e4c5ac0b32a57c58c0e529c001'
$setup = GetPinnedAsset 'PawnIO_setup.exe' 'https://github.com/namazso/PawnIO.Setup/releases/download/2.2.0/PawnIO_setup.exe' `
    '1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032'
$modules = GetPinnedAsset 'PawnIO.Modules-0.2.12.zip' 'https://github.com/namazso/PawnIO.Modules/releases/download/0.2.12/release_0_2_12.zip' `
    '09698897ce19b92f1043a2e322cfcf0b1fe823e0b576971b178a65244fb0af9d'

$libraries = @(
    'LibreHardwareMonitorLib.dll', 'System.Memory.dll', 'RAMSPDToolkit-NDD.dll',
    'HidSharp.dll', 'DiskInfoToolkit.dll', 'System.Runtime.CompilerServices.Unsafe.dll',
    'BlackSharp.Core.dll', 'System.Numerics.Vectors.dll', 'System.Buffers.dll'
)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($name in $libraries) {
        $entry = $zip.GetEntry($name)
        if (!$entry -or $entry.Length -le 0 -or $entry.Length -gt 20MB) { throw "Invalid bundled library: $name" }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $output $name), $true)
    }
} finally { $zip.Dispose() }

$gtModule = Join-Path $output 'IntelMCHBAR.bin'
$moduleLicense = Join-Path $output 'PawnIO-Modules-COPYING.txt'
$zip = [IO.Compression.ZipFile]::OpenRead($modules)
try {
    [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry('IntelMCHBAR.bin'), $gtModule, $true)
    [IO.Compression.ZipFileExtensions]::ExtractToFile($zip.GetEntry('COPYING'), $moduleLicense, $true)
} finally { $zip.Dispose() }

$compiler = Join-Path ([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()) 'csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework C# compiler is required' }
$bridge = Join-Path $output 'TaskbarMonitor.Sensors.dll'
& $compiler /nologo /optimize+ /target:library "/out:$bridge" /reference:System.dll /reference:System.Management.dll `
    "/reference:$(Join-Path $output 'LibreHardwareMonitorLib.dll')" "/resource:$gtModule,TaskbarMonitor.Sensors.IntelMCHBAR.bin" `
    (Join-Path $root 'src\SensorBridge.cs') (Join-Path $root 'src\HardwareEngine.cs') `
    (Join-Path $root 'src\IntelGtTemperature.cs') (Join-Path $root 'src\AmdAdlxTemperature.cs') `
    (Join-Path $root 'src\AdditionalSensors.cs')
if ($LASTEXITCODE -ne 0) { throw 'Could not compile the embedded sensor bridge' }

$notice = @"
TaskbarMonitor embeds LibreHardwareMonitorLib $version (MPL 2.0) and its
unmodified library dependencies for direct CPU and GPU temperature readings.
Source: $lhm/tree/v$version

The unmodified, signed PawnIO 2.2.0 installer is redistributed separately
from the sensor library and only runs when the user approves installing it for sensor access.
Installer: https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0
Driver source: https://github.com/namazso/PawnIO

The unmodified IntelMCHBAR module from PawnIO.Modules 0.2.12 (LGPL 2.1 or later)
reads the integrated Intel graphics temperature. Its license follows below.
Module source: https://github.com/namazso/PawnIO.Modules/tree/0.2.12
"@
$notice += "`r`n`r`n" + [IO.File]::ReadAllText($moduleLicense)
foreach ($name in @('LICENSE', 'THIRD-PARTY-NOTICES.txt')) {
    $path = Join-Path $output "Upstream-$name"
    if (!(Test-Path -LiteralPath $path)) {
        Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/LibreHardwareMonitor/LibreHardwareMonitor/v$version/$name" -OutFile $path
    }
    $notice += "`r`n`r`n" + [IO.File]::ReadAllText($path)
}
[IO.File]::WriteAllText((Join-Path $output 'ThirdParty-Licenses.txt'), $notice, [Text.UTF8Encoding]::new($false))

$resources = @('#pragma code_page(65001)', "2511 RCDATA `"$($setup.Replace('\', '\\'))`"")
$names = @('STRINGTABLE', 'BEGIN')
$id = 2500
foreach ($name in ($libraries + @('TaskbarMonitor.Sensors.dll', 'ThirdParty-Licenses.txt'))) {
    $path = (Join-Path $output $name).Replace('\', '\\').Replace('"', '\"')
    $resources += "$id RCDATA `"$path`""
    $names += "    $id `"$name`""
    ++$id
}
$names += 'END'
[IO.File]::WriteAllLines((Join-Path $output 'EmbeddedSensors.rc'), ($resources + $names), [Text.UTF8Encoding]::new($false))
