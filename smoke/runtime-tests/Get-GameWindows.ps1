param([Parameter(Mandatory)][int]$ProcessId, [string]$Desktop = '')
# Lists a process's top-level windows (class, title, visibility) and the text of dialog
# controls, on the current desktop or a named one. Used to read error dialogs of hidden runs.
if (-not ('GameWindows' -as [type])) {
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public static class GameWindows {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr d, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr OpenDesktop(string n, int f, bool i, uint a);
  [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr d);
  [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, int m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  static string Text(IntPtr h) { var t = new StringBuilder(4096); SendMessage(h, 0x000D, (IntPtr)4096, t); return t.ToString(); }
  public static List<string> For(int pid, string desktop) {
    var r = new List<string>();
    EnumProc top = (h, l) => {
      int p; GetWindowThreadProcessId(h, out p);
      if (p != pid) return true;
      var c = new StringBuilder(256); GetClassName(h, c, 256);
      r.Add(c + " | " + Text(h) + " | visible=" + IsWindowVisible(h));
      EnumChildWindows(h, (k, m) => { var cc = new StringBuilder(256); GetClassName(k, cc, 256); var t = Text(k); if (t.Length > 0) r.Add("   " + cc + ": " + t); return true; }, IntPtr.Zero);
      return true; };
    if (string.IsNullOrEmpty(desktop)) EnumWindows(top, IntPtr.Zero);
    else { var d = OpenDesktop(desktop, 0, false, 0x0001 | 0x0040); if (d != IntPtr.Zero) { EnumDesktopWindows(d, top, IntPtr.Zero); CloseDesktop(d); } else r.Add("cannot open desktop " + desktop); }
    return r; }
}
"@
}
[GameWindows]::For($ProcessId, $Desktop)
