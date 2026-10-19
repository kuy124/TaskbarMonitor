# Applies a local code signature so the build does not trigger the
# "Unknown Publisher" warning on this machine.
$ErrorActionPreference = 'Stop'
$target = $args[0]
if (-not $target) { $target = 'TaskbarMonitor.exe' }

$subject = 'CN=TaskbarMonitor Developer'

function Get-TrustedThumbprints {
    (Get-ChildItem 'Cert:\CurrentUser\Root' |
        Where-Object { $_.Subject -eq $subject } |
        ForEach-Object { $_.Thumbprint })
}

# Prefer a certificate that is already trusted locally, so the release build
# does not have to touch the root store (which prompts for confirmation).
$trusted = @(Get-TrustedThumbprints)
$cert = Get-ChildItem 'Cert:\CurrentUser\My' |
        Where-Object {
            $_.Subject -eq $subject -and $_.HasPrivateKey -and
            $_.NotAfter -gt (Get-Date) -and $trusted -contains $_.Thumbprint
        } |
        Select-Object -First 1

if (-not $cert) {
    $cert = Get-ChildItem 'Cert:\CurrentUser\My' |
            Where-Object {
                $_.Subject -eq $subject -and $_.HasPrivateKey -and
                $_.NotAfter -gt (Get-Date)
            } |
            Select-Object -First 1
}

if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject `
            -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddYears(10)
}

if ($trusted -notcontains $cert.Thumbprint) {
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store('Root', 'CurrentUser')
    $store.Open('ReadWrite')
    $store.Add($cert)
    $store.Close()
}

Set-AuthenticodeSignature -FilePath $target -Certificate $cert | Out-Null
Unblock-File -Path $target -ErrorAction SilentlyContinue

$status = (Get-AuthenticodeSignature $target).Status
if ($status -eq 'Valid') {
    Write-Host '[OK] Digitally signed and trusted on this system!'
} else {
    Write-Host ('[WARN] Signature status: ' + $status)
}
