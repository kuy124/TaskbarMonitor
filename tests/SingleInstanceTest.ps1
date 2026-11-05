param([Parameter(Mandatory = $true)][string]$Executable)

$ErrorActionPreference = 'Stop'
$mutex = New-Object System.Threading.Mutex($false, 'Local\TaskbarMonitor.SingleInstance')
try {
    foreach ($arguments in @('--settings', '--autostart')) {
        $process = Start-Process -FilePath $Executable -ArgumentList $arguments -PassThru
        try {
            if (-not $process.WaitForExit(5000)) {
                throw "Duplicate launch ($arguments) did not exit."
            }
            if ($process.ExitCode -ne 0) {
                throw "Duplicate launch ($arguments) failed with exit code $($process.ExitCode)."
            }
        } finally {
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
    }
} finally {
    $mutex.Dispose()
}
