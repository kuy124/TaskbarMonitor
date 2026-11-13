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
    Record "64-bit: $([Environment]::Is64BitProcess); backend status: $([TaskbarMonitor.Sensors.Bootstrap]::Status('')) (bit 1 PawnIO, bit 2 administrator)"
    Get-CimInstance Win32_VideoController | ForEach-Object { Record "GPU: $($_.Name); driver: $($_.DriverVersion)" }
    $assembly = [TaskbarMonitor.Sensors.Bootstrap].Assembly
    $reader = $assembly.GetType('TaskbarMonitor.Sensors.Reader')
    $extra = $assembly.GetType('TaskbarMonitor.Sensors.AdditionalSensors')
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
    for ($sample = 0; $sample -lt 3; ++$sample) {
        $packed = [TaskbarMonitor.Sensors.Bootstrap]::Read('')
        Record "Sample ${sample}: CPU tenths=$($packed -band 65535); GPU tenths=$(($packed -shr 16) -band 65535); sources=$([TaskbarMonitor.Sensors.Bootstrap]::Sources('')) (65535=unavailable)"
        Start-Sleep -Seconds 1
    }
    $computer = $reader.GetField('computer', $flags).GetValue($null)
    Record ($computer.GetReport())
    foreach ($method in @('ReadDriverGpuTemperature')) {
        try { Record "${method}: $($reader.GetMethod($method, $flags).Invoke($null, $null))" }
        catch { Record "${method}: $($_.Exception.GetBaseException().Message)" }
    }
    foreach ($method in @('IntelTemperature', 'SysmanTemperature')) {
        try { Record "${method}: $($extra.GetMethod($method, $flags).Invoke($null, $null))" }
        catch { Record "${method}: $($_.Exception.GetBaseException().Message)" }
    }
    $igcl = $computer.GetType().Assembly.GetType('LibreHardwareMonitor.Interop.IntelGcl')
    $devices = $igcl.GetMethod('GetDeviceHandles').Invoke($null, $null)
    Record "IGCL device count: $($devices.Length)"
    $initType = $igcl.GetNestedType('ctl_init_args_t')
    $apiType = $igcl.GetNestedType('ctl_api_handle_t')
    foreach ($initFlags in @(1, 0)) {
        $init = [Activator]::CreateInstance($initType)
        $init.Size = [Runtime.InteropServices.Marshal]::SizeOf($init)
        $init.AppVersion = 65537
        $init.flags = $initFlags
        $arguments = @($init, [Activator]::CreateInstance($apiType))
        $result = $igcl.GetMethod('ctlInit', $flags).Invoke($null, $arguments)
        Record "IGCL initialization flags=${initFlags}: result=$('{0:X8}' -f $result)"
        if ($result -eq 0) { $igcl.GetMethod('ctlClose', $flags).Invoke($null, @($arguments[1])) | Out-Null }
    }
    foreach ($device in $devices) {
        $handle = $extra.GetMethod('IntelHandle', $flags).Invoke($null, @($device))
        $arguments = @($handle, [uint32]0, $null)
        $result = $extra.GetMethod('ctlEnumTemperatureSensors', $flags).Invoke($null, $arguments)
        Record "IGCL temperature enumeration: result=$result; count=$($arguments[1])"
    }
    try { Record "Sysman initialization result: $($extra.GetMethod('zesInit', $flags).Invoke($null, @([uint32]0)))" }
    catch { Record "Sysman initialization: $($_.Exception.GetBaseException().Message)" }
    $arguments = @([uint32]0, $null)
    $result = $extra.GetMethod('zesDriverGet', $flags).Invoke($null, $arguments)
    Record "Sysman driver enumeration: result=$('{0:X8}' -f $result); count=$($arguments[0])"
    if ($result -eq 0 -and $arguments[0] -gt 0 -and $arguments[0] -le 64) {
        $drivers = [IntPtr[]]::new($arguments[0])
        $arguments = @($arguments[0], $drivers)
        $extra.GetMethod('zesDriverGet', $flags).Invoke($null, $arguments) | Out-Null
        foreach ($driver in $drivers) {
            $arguments = @($driver, [uint32]0, $null)
            $result = $extra.GetMethod('zesDeviceGet', $flags).Invoke($null, $arguments)
            Record "Sysman device enumeration: result=$('{0:X8}' -f $result); count=$($arguments[1])"
            if ($result -eq 0 -and $arguments[1] -gt 0 -and $arguments[1] -le 64) {
                $deviceHandles = [IntPtr[]]::new($arguments[1])
                $arguments = @($driver, $arguments[1], $deviceHandles)
                $extra.GetMethod('zesDeviceGet', $flags).Invoke($null, $arguments) | Out-Null
                foreach ($deviceHandle in $deviceHandles) {
                    $arguments = @($deviceHandle, [uint32]0, $null)
                    $result = $extra.GetMethod('zesDeviceEnumTemperatureSensors', $flags).Invoke($null, $arguments)
                    Record "Sysman temperature enumeration: result=$('{0:X8}' -f $result); count=$($arguments[1])"
                }
            }
        }
    }
    [TaskbarMonitor.Sensors.Bootstrap]::SelfCheck('') | Out-Null
    Record 'Managed regression checks passed'
}
catch { Record "FAILED: $($_.Exception.ToString())"; throw }
finally {
    if ('TaskbarMonitor.Sensors.Bootstrap' -as [type]) { [TaskbarMonitor.Sensors.Bootstrap]::Close('') | Out-Null }
    $report | Set-Content -LiteralPath $OutputPath -Encoding UTF8
}
