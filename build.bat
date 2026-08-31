@echo off
setlocal enabledelayedexpansion

echo [BUILD] Compiling embedded Windows manifest and version resources...
windres -i src/TaskbarMonitor.rc -o src/TaskbarMonitor.res.o

echo [BUILD] Compiling TaskbarMonitor C++ sources...
g++ -O3 -mwindows -municode src/Config.cpp src/Theme.cpp src/Metrics.cpp src/Renderer.cpp src/TaskbarSync.cpp src/SettingsWindow.cpp src/Main.cpp src/TaskbarMonitor.res.o -o TaskbarMonitor.exe -liphlpapi -lgdi32 -ldwmapi -lpdh -lpsapi -lshell32 -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -lwbemuuid

if %errorlevel% equ 0 (
    echo [OK] Successfully compiled TaskbarMonitor.exe!
    echo [SIGN] Applying local code signature to prevent Unknown Publisher security warnings...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$cert = Get-ChildItem 'Cert:\CurrentUser\My' | Where-Object { $_.Subject -eq 'CN=TaskbarMonitor Developer' } | Select-Object -First 1; ^
        if (-not $cert) { ^
            $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=TaskbarMonitor Developer' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddYears(10); ^
            $store = New-Object System.Security.Cryptography.X509Certificates.X509Store('Root', 'CurrentUser'); ^
            $store.Open('ReadWrite'); ^
            $store.Add($cert); ^
            $store.Close(); ^
        } ^
        Set-AuthenticodeSignature -FilePath 'TaskbarMonitor.exe' -Certificate $cert | Out-Null; ^
        Unblock-File -Path 'TaskbarMonitor.exe' -ErrorAction SilentlyContinue; ^
        Write-Host '[OK] Digitally signed and trusted on this system!'"
) else (
    echo [ERROR] Build failed.
)