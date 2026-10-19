param([int]$Samples = 30, [string]$Screenshot = '')
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
        Attached = ([TaskbarProbe]::GetParent($handle) -eq [TaskbarProbe]::FindWindow('Shell_TrayWnd', $null))
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
