param(
    [string]$SensorDirectory = "$PSScriptRoot/../build-compat/sensors",
    [string]$OutputPath = "$PSScriptRoot/../build-compat/temperature-device.txt"
)
$ErrorActionPreference = 'Stop'
$report = [Collections.Generic.List[string]]::new()
function Record($message) { $report.Add([string]$message); Write-Host $message }
try {
    $directory = (Resolve-Path $SensorDirectory).Path
    [Reflection.Assembly]::LoadFrom("$directory/TaskbarMonitor.Sensors.dll") | Out-Null
    [TaskbarMonitor.Sensors.Bootstrap]::Initialize($directory) | Out-Null
    Record "Date: $(Get-Date -Format o)"
    Record "64-bit: $([Environment]::Is64BitProcess); backend status: $([TaskbarMonitor.Sensors.Bootstrap]::Status('')) (bit 1 native sensor API, bit 2 administrator)"
    Get-CimInstance Win32_VideoController | ForEach-Object { Record "GPU: $($_.Name); driver: $($_.DriverVersion)" }
    $assembly = [TaskbarMonitor.Sensors.Bootstrap].Assembly
    $extra = $assembly.GetType('TaskbarMonitor.Sensors.AdditionalSensors')
    $engine = $assembly.GetType('TaskbarMonitor.Sensors.HardwareEngine')
    $telemetry = $assembly.GetType('TaskbarMonitor.Sensors.NitroSenseTelemetry')
    $flags = [Reflection.BindingFlags]'NonPublic,Static'
    try {
        $arguments = [object[]]@([double]-1, [double]-1)
        $extra.GetMethod('Hwinfo', $flags).Invoke($null, $arguments) | Out-Null
        Record "HWiNFO direct: CPU=$($arguments[0]); GPU=$($arguments[1])"
    } catch { Record "HWiNFO direct FAILED: $($_.Exception.GetBaseException())" }
    try {
        $map = [IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting('Global\HWiNFO_SENS_SM2', [IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
        $view = $map.CreateViewAccessor(0, 0, [IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
        $data = [byte[]]::new($view.Capacity)
        $view.ReadArray(0, $data, 0, $data.Length) | Out-Null
        $view.Dispose(); $map.Dispose()
        $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
        $arguments = [object[]]@($data, $now, [double]-1, [double]-1)
        $extra.GetMethod('ParseHwinfo', $flags).Invoke($null, $arguments) | Out-Null
        Record "HWiNFO map: length=$($data.Length); age=$($now-[BitConverter]::ToInt64($data,12)); CPU=$($arguments[2]); GPU=$($arguments[3])"
    } catch { Record "HWiNFO map FAILED: $($_.Exception.GetBaseException())" }
    foreach ($method in @('WmiThermalZone', 'WmiTemperatureProbe')) {
        try {
            $arguments = [object[]]@([double]-1, [int]0)
            $engine.GetMethod($method, $flags).Invoke($null, $arguments) | Out-Null
            Record "${method}: value=$($arguments[0]); source=$($arguments[1])"
        } catch { Record "${method}: $($_.Exception.GetBaseException().Message)" }
    }
    foreach ($method in @('WddmGpu', 'IntelGpu', 'SysmanGpu', 'NvmlGpu', 'AdlGpu')) {
        try {
            $arguments = [object[]]@([double]-1, [int]0)
            $engine.GetMethod($method, $flags).Invoke($null, $arguments) | Out-Null
            Record "${method}: value=$($arguments[0]); source=$($arguments[1])"
        } catch { Record "${method}: $($_.Exception.GetBaseException().Message)" }
    }
    for ($sample = 0; $sample -lt 12; ++$sample) {
        $packed = [TaskbarMonitor.Sensors.Bootstrap]::Read('')
        Record "Sample ${sample}: CPU tenths=$($packed -band 65535); GPU tenths=$(($packed -shr 16) -band 65535); sources=$([TaskbarMonitor.Sensors.Bootstrap]::Sources('')) (65535=unavailable)"
        Start-Sleep -Seconds 1
    }
    Record "NitroSense telemetry: CPU=$($telemetry.GetField('cachedCpu', $flags).GetValue($null)); GPU=$($telemetry.GetField('cachedGpu', $flags).GetValue($null)); updated=$($telemetry.GetField('lastUpdate', $flags).GetValue($null))"
    Record "NitroSense certificate: $($telemetry.GetField('certificateError', $flags).GetValue($null))"
    Record "NitroSense last error: $($telemetry.GetField('lastError', $flags).GetValue($null))"
    [TaskbarMonitor.Sensors.Bootstrap]::SelfCheck('') | Out-Null
    Record 'Managed regression checks passed'
}
catch { Record "FAILED: $($_.Exception.ToString())"; throw }
finally {
    if ('TaskbarMonitor.Sensors.Bootstrap' -as [type]) { [TaskbarMonitor.Sensors.Bootstrap]::Close('') | Out-Null }
    $report | Set-Content -LiteralPath $OutputPath -Encoding UTF8
}
