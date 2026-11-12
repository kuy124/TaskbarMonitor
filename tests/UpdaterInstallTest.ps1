# Called by Updater.ps1 -SelfTest; no network, application settings, or real installation touched.
$root = Join-Path ([IO.Path]::GetTempPath()) ('TaskbarMonitor-update-test-' + [guid]::NewGuid().ToString('N'))
$running = New-Object 'Collections.Generic.List[Diagnostics.Process]'
$lastRejection = ''
function Assert-Rejected([scriptblock]$Action) {
    try {
        $result = & $Action
        if ($result -is [Diagnostics.Process]) { $running.Add($result) }
    } catch { $script:lastRejection = $_.Exception.Message; return }
    throw 'Expected update operation to be rejected'
}
function New-TestZip([string]$Path, [string[]]$Names, [string]$Content) {
    $zip = [IO.Compression.ZipFile]::Open($Path, 'Create')
    try {
        foreach ($name in $Names) {
            $stream = $zip.CreateEntry($name).Open()
            try {
                if ($Content) {
                    $input = [IO.File]::OpenRead($Content)
                    try { $input.CopyTo($stream) } finally { $input.Dispose() }
                }
            } finally { $stream.Dispose() }
        }
    } finally { $zip.Dispose() }
}
try {
    New-Item -ItemType Directory -Path $root | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($fixture in @(
        @{ Name = 'old'; Version = '1.2.4.0'; Body = 'System.Threading.Thread.Sleep(60000);' },
        @{ Name = 'new'; Version = '1.2.5.0'; Body = 'System.Threading.Thread.Sleep(60000);' },
        @{ Name = 'exit'; Version = '1.2.5.0'; Body = 'return;' }
    )) {
        $code = '[assembly: System.Reflection.AssemblyFileVersion("' + $fixture.Version + '")] ' +
                'public static class UpdateFixture' + $fixture.Name + ' { public static void Main() { ' + $fixture.Body + ' } }'
        Add-Type -TypeDefinition $code -OutputAssembly (Join-Path $root ($fixture.Name + '.exe')) -OutputType WindowsApplication
    }
    $old = Join-Path $root 'old.exe'
    $new = Join-Path $root 'new.exe'
    $exit = Join-Path $root 'exit.exe'
    $oldHash = (Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash

    $archive = Join-Path $root 'release.zip'
    $escapedName = 'TaskbarMonitor-escape-test-' + [guid]::NewGuid().ToString('N') + '.exe'
    $escapedPath = Join-Path ([IO.Path]::GetDirectoryName($root)) $escapedName
    New-TestZip $archive @('TaskbarMonitor/TaskbarMonitor.exe', ('../' + $escapedName)) $new
    $asset = [pscustomobject]@{
        size = (Get-Item -LiteralPath $archive).Length
        digest = 'sha256:' + (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    Test-UpdateDownload $archive $asset
    $asset.size++
    Assert-Rejected { Test-UpdateDownload $archive $asset }
    $asset.size--
    $digest = $asset.digest
    $asset.digest = 'sha256:' + ('0' * 64)
    Assert-Rejected { Test-UpdateDownload $archive $asset }
    $asset.digest = $digest
    $extracted = Join-Path $root 'extracted.exe'
    Expand-Update $archive $extracted ([version]'1.2.5.0')
    if ((Get-FileHash -LiteralPath $extracted -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $new -Algorithm SHA256).Hash) {
        throw 'Wrong executable extracted'
    }
    if (Test-Path -LiteralPath $escapedPath) { throw 'Archive escaped its staging directory' }
    Assert-Rejected { Expand-Update $archive (Join-Path $root 'mismatch.exe') ([version]'1.2.6.0') }
    $duplicate = Join-Path $root 'duplicate.zip'
    New-TestZip $duplicate @('TaskbarMonitor/TaskbarMonitor.exe', 'TaskbarMonitor/TaskbarMonitor.exe') $new
    Assert-Rejected { Expand-Update $duplicate (Join-Path $root 'duplicate.exe') ([version]'1.2.5.0') }
    $missing = Join-Path $root 'missing.zip'
    New-TestZip $missing @('../TaskbarMonitor.exe') $new
    Assert-Rejected { Expand-Update $missing (Join-Path $root 'missing.exe') ([version]'1.2.5.0') }
    $empty = Join-Path $root 'empty.zip'
    New-TestZip $empty @('TaskbarMonitor/TaskbarMonitor.exe') ''
    Assert-Rejected { Expand-Update $empty (Join-Path $root 'empty.exe') ([version]'1.2.5.0') }
    $invalid = Join-Path $root 'invalid.zip'
    [IO.File]::WriteAllText($invalid, 'not a ZIP')
    Assert-Rejected { Expand-Update $invalid (Join-Path $root 'invalid.exe') ([version]'1.2.5.0') }

    $installed = Join-Path $root 'installed.exe'
    $replacement = Join-Path $root 'replacement.exe'
    $backup = Join-Path $root 'previous.exe'
    Copy-Item -LiteralPath $old -Destination $installed
    Copy-Item -LiteralPath $new -Destination $replacement
    $held = [IO.File]::Open($installed, 'Open', 'Read', 'None')
    try { Assert-Rejected { Install-Update $replacement $installed $backup } } finally { $held.Dispose() }
    if ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne $oldHash) { throw 'Failed replacement damaged the original' }

    $backup = Join-Path $root 'restart-previous.exe'
    Copy-Item -LiteralPath $exit -Destination $replacement -Force
    Assert-Rejected { Install-Update $replacement $installed $backup }
    if ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne $oldHash -or (Test-Path -LiteralPath $backup)) {
        throw "Failed restart did not roll back: $lastRejection"
    }

    $backup = Join-Path $root 'rollback-previous.exe'
    Copy-Item -LiteralPath $exit -Destination $replacement -Force
    $normalReplace = ${function:Replace-Executable}
    try {
        # Deterministic equivalent of another process locking the destination during rollback.
        ${function:Replace-Executable} = {
            param($Source, $Destination, $Backup)
            if (!$Backup) { throw 'Simulated locked rollback destination' }
            [IO.File]::Replace($Source, $Destination, $Backup)
        }
        Assert-Rejected { Install-Update $replacement $installed $backup }
        if (!(Test-Path -LiteralPath $backup) -or (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $oldHash -or !$lastRejection.Contains($backup)) {
            throw 'Failed rollback did not preserve and identify the original copy'
        }
    } finally { ${function:Replace-Executable} = $normalReplace }
    Replace-Executable $backup $installed $null

    $backup = Join-Path $root 'success-previous.exe'
    Copy-Item -LiteralPath $new -Destination $replacement -Force
    $running.Add((Install-Update $replacement $installed $backup))
    if ($running[0].HasExited -or (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $new -Algorithm SHA256).Hash) {
        throw 'Successful update did not stay running'
    }
    if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash -ne $oldHash) { throw 'Successful replacement lost the original backup' }
    Write-Output 'Updater download, extraction, replacement, restart, and rollback tests passed'
} finally {
    foreach ($process in $running) {
        if (!$process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit(5000) | Out-Null
        }
        $process.Dispose()
    }
    if ($escapedPath) { Remove-Item -LiteralPath $escapedPath -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
