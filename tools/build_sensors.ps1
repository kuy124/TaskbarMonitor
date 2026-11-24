param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null

$compiler = Join-Path ([Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()) 'csc.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw '.NET Framework C# compiler is required' }
$bridge = Join-Path $output 'TaskbarMonitor.Sensors.dll'
& $compiler /nologo /optimize+ /target:library "/out:$bridge" /reference:System.dll /reference:System.Management.dll (Join-Path $root 'src\SensorBridge.cs') (Join-Path $root 'src\HardwareEngine.cs') (Join-Path $root 'src\AdditionalSensors.cs')
if ($LASTEXITCODE -ne 0) { throw 'Could not compile the embedded sensor bridge' }

$notice = @"
TaskbarMonitor's hardware sensor engine is built into the application and uses
Windows APIs and optional vendor libraries already present on the system.
No third-party sensor library is bundled by this build.

Source files: src/SensorBridge.cs, src/HardwareEngine.cs, src/AdditionalSensors.cs
"@
[IO.File]::WriteAllText((Join-Path $output 'ThirdParty-Licenses.txt'), $notice, [Text.UTF8Encoding]::new($false))

$resources = @('#pragma code_page(65001)')
$names = @('STRINGTABLE', 'BEGIN')
$entries = @(
    @{ Id = 2500; File = 'TaskbarMonitor.Sensors.dll' },
    @{ Id = 2510; File = 'ThirdParty-Licenses.txt' }
)
foreach ($entry in $entries) {
    $path = (Join-Path $output $entry.File).Replace('\', '\\').Replace('"', '\"')
    $resources += "$($entry.Id) RCDATA `"$path`""
    $names += "    $($entry.Id) `"$($entry.File)`""
}
$names += 'END'
[IO.File]::WriteAllLines((Join-Path $output 'EmbeddedSensors.rc'), ($resources + $names), [Text.UTF8Encoding]::new($false))
