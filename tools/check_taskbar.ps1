param([int]$Samples = 30, [string]$Screenshot = '', [switch]$StartSearch)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class TaskbarProbe {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder name, int size);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsChild(IntPtr parent, IntPtr child);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    public static IntPtr Monitor() {
        IntPtr found = FindWindow("TaskbarMonitor", null);
        if (found != IntPtr.Zero) return found;
        EnumChildWindows(FindWindow("Shell_TrayWnd", null), (h,d) => {
            var name = new System.Text.StringBuilder(128); GetClassName(h, name, 128);
            if (name.ToString() == "TaskbarMonitor") found = h;
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$observations = @()
for ($i=0; $i -lt $Samples; $i++) {
    $handle = [TaskbarProbe]::Monitor()
    $rect = New-Object TaskbarProbe+Rect
    [void][TaskbarProbe]::GetWindowRect($handle, [ref]$rect)
    $observations += [pscustomobject]@{
        Visible = [TaskbarProbe]::IsWindowVisible($handle)
        Attached = [TaskbarProbe]::IsChild([TaskbarProbe]::FindWindow('Shell_TrayWnd', $null), $handle)
        X = $rect.Left; Y = $rect.Top; Width = $rect.Right-$rect.Left
    }
    Start-Sleep -Milliseconds 200
}
$observations | Group-Object Visible,Attached,X,Y,Width | Select-Object Count,Name | Format-Table | Out-String | Write-Output
if ($Screenshot) {
    Add-Type -AssemblyName System.Drawing
    $rect = New-Object TaskbarProbe+Rect
    [void][TaskbarProbe]::GetWindowRect([TaskbarProbe]::FindWindow('Shell_TrayWnd', $null), [ref]$rect)
    if ($rect.Right -le $rect.Left -or $rect.Bottom -le $rect.Top) {
        throw 'No taskbar rectangle is available in this desktop session.'
    }
    $bitmap = New-Object System.Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left,$rect.Top,0,0,$bitmap.Size)
        $bitmap.Save($Screenshot)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
if ($observations.Where({-not $_.Visible -or -not $_.Attached}).Count) {
    throw 'Monitor was hidden or detached during the sample. Check whether the taskbar had space and was open.'
}
if ($StartSearch) {
    # Keep the original HWND: enumeration cannot find it while the panels are open.
    $handle = [TaskbarProbe]::Monitor()
    foreach ($panel in @('Start', 'Search')) {
        try {
            [TaskbarProbe]::keybd_event(0x5B, 0, 0, [UIntPtr]::Zero)
            if ($panel -eq 'Search') {
                [TaskbarProbe]::keybd_event(0x53, 0, 0, [UIntPtr]::Zero)
                [TaskbarProbe]::keybd_event(0x53, 0, 2, [UIntPtr]::Zero)
            }
            [TaskbarProbe]::keybd_event(0x5B, 0, 2, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 800
            for ($i=0; $i -lt $Samples; $i++) {
                $taskbar = [TaskbarProbe]::FindWindow('Shell_TrayWnd', $null)
                if (-not [TaskbarProbe]::IsWindowVisible($handle) -or
                    -not [TaskbarProbe]::IsChild($taskbar, $handle)) {
                    throw "Monitor was hidden, detached, or replaced while $panel was open."
                }
                Start-Sleep -Milliseconds 200
            }
        } finally {
            [TaskbarProbe]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
            [TaskbarProbe]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
            Start-Sleep -Milliseconds 500
        }
    }
    'Start and Search visibility checks passed.'
}
