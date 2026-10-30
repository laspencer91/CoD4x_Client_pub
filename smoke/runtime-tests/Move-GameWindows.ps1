param([Parameter(Mandatory)][int]$ProcessId, [int]$Seconds = 0)
# Moves every visible top-level window of a test process off-screen without activating it,
# so a windowed test client keeps rendering but is not shown on the user's desktop.
# With -Seconds, keeps doing so for that long (the game recreates its window on vid_restart).
if (-not ('OffscreenWindows' -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class OffscreenWindows {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public static int Move(int pid) {
    int moved = 0;
    EnumWindows((h, l) => {
      int p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true;
      RECT r; GetWindowRect(h, out r);
      if (r.Left > -20000) { SetWindowPos(h, (IntPtr)1, -30000, -30000, 0, 0, 0x0001 | 0x0010 | 0x0200); moved++; }
      return true; }, IntPtr.Zero);
    return moved;
  }
}
"@
}
$deadline = (Get-Date).AddSeconds($Seconds)
do {
    [void][OffscreenWindows]::Move($ProcessId)
    if ($Seconds -gt 0) { Start-Sleep -Milliseconds 200 }
} while ((Get-Date) -lt $deadline -and (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue))
