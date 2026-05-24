# Harness: launch ui_theme_demo at multiple window sizes, capture screenshot
# + stderr log per size. Run from project root.

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int W, int H, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
'@
[W]::SetProcessDPIAware() | Out-Null

$exe = "C:/projects/neotolis-engine/build/examples/ui_theme_demo/native-debug/ui_theme_demo.exe"
$workdir = "C:/projects/neotolis-engine/build/examples/ui_theme_demo/native-debug"
$outdir = "C:/projects/neotolis-engine/scripts/_demo_test"
New-Item -ItemType Directory -Force -Path $outdir | Out-Null

# Test sizes: (label, w, h)
$sizes = @(
    @{ name = "960x640_1to1";  w = 960;  h = 640 },
    @{ name = "1920x1080_big"; w = 1920; h = 1080 },
    @{ name = "480x320_half";  w = 480;  h = 320 },
    @{ name = "1280x720_wide"; w = 1280; h = 720 },
    @{ name = "640x960_tall";  w = 640;  h = 960 }
)

foreach ($s in $sizes) {
    Write-Host "=== $($s.name) [$($s.w)x$($s.h)] ==="
    $log = "$outdir/$($s.name).log"
    # Stop any leftover process first
    Get-Process -Name ui_theme_demo -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    # Launch
    $p = Start-Process -FilePath $exe -WorkingDirectory $workdir -RedirectStandardError $log -PassThru
    Start-Sleep -Seconds 4
    # Position + resize (account for window chrome ~30px tall)
    [W]::MoveWindow($p.MainWindowHandle, 100, 100, $s.w, $s.h + 30, $true) | Out-Null
    Start-Sleep -Seconds 2
    # Capture at physical fb size (account for DPI scaling)
    $r = New-Object W+RECT
    [W]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $cw = $r.R - $r.L
    $ch = $r.B - $r.T
    $dpi = [W]::GetDpiForWindow($p.MainWindowHandle)
    $scale = $dpi / 96.0
    $fbw = [int]([Math]::Round($cw * $scale))
    $fbh = [int]([Math]::Round($ch * $scale))
    $bmp = New-Object System.Drawing.Bitmap $fbw, $fbh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [W]::PrintWindow($p.MainWindowHandle, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc)
    $bmp.Save("$outdir/$($s.name).png")
    $bmp.Dispose()
    $g.Dispose()
    Write-Host "  client=${cw}x${ch} dpi=${dpi} bmp=${fbw}x${fbh} -> $outdir/$($s.name).png"
    # Kill
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

Write-Host ""
Write-Host "Done. Results in $outdir"
