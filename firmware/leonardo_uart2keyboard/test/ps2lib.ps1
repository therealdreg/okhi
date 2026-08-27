# MIT License - okhi - Open Keylogger Hardware Implant
# ---------------------------------------------------------------------------
# Copyright (c) [2024] by David Reguera Garcia aka Dreg
# https://github.com/therealdreg/okhi
# https://www.rootkit.es
# X @therealdreg
# dreg@rootkit.es
# ---------------------------------------------------------------------------
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# ---------------------------------------------------------------------------
# WARNING: BULLSHIT CODE X-)
# ---------------------------------------------------------------------------

# PS/2 keyboard hardware test helper: safe focus + UIAutomation readback + serial.
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,WindowsBase
# A .NET type cannot be redefined once loaded, so a shell that already dot-sourced an older copy
# of this file needs a fresh PowerShell process. The tests always run in their own process.
if (-not ("WinFocus" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinFocus {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();

  // Windows refuses SetForegroundWindow to a process that is not already in the foreground: the
  // call returns false and nothing happens. Attaching our input queue to the current foreground
  // thread lifts that restriction for the duration of the call, which is what makes focus
  // acquisition reliable when the tests are launched from another window.
  public static bool ForceForeground(IntPtr h) {
    if (h == IntPtr.Zero) return false;
    if (IsIconic(h)) ShowWindow(h, 9);
    IntPtr fg = GetForegroundWindow();
    if (fg == h) return true;
    uint self = GetCurrentThreadId();
    uint other = (fg == IntPtr.Zero) ? 0 : GetWindowThreadProcessId(fg, IntPtr.Zero);
    bool attached = (other != 0 && other != self) ? AttachThreadInput(self, other, true) : false;
    try {
      BringWindowToTop(h);
      SetForegroundWindow(h);
      SetFocus(h);
    } finally {
      if (attached) AttachThreadInput(self, other, false);
    }
    return GetForegroundWindow() == h;
  }
}
"@
}

function Get-NpHandle {
  $np = Get-Process notepad -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $np) { return [IntPtr]::Zero }
  return $np.MainWindowHandle
}

function Get-NpDoc {
  $h = Get-NpHandle
  if ($h -eq [IntPtr]::Zero) { return $null }
  $root = [System.Windows.Automation.AutomationElement]::FromHandle($h)
  if (-not $root) { return $null }
  $condDoc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Document)
  $doc = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condDoc)
  if (-not $doc) {
    $condEdit = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::Edit)
    $doc = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condEdit)
  }
  return $doc
}

function Read-Np {
  $doc = Get-NpDoc
  if (-not $doc) { return $null }
  $vp = $null
  if ($doc.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) {
    return $vp.Current.Value
  }
  $tp = $null
  if ($doc.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref]$tp)) {
    return $tp.DocumentRange.GetText(-1)
  }
  return $null
}

function Clear-Np {
  $doc = Get-NpDoc
  if (-not $doc) { return $false }
  $vp = $null
  if ($doc.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) {
    try { $vp.SetValue("") } catch { return $false }
    Start-Sleep -Milliseconds 120
    $v = Read-Np
    return ($v -eq "" -or $v -eq $null)
  }
  return $false
}

# Force Notepad to foreground and CONFIRM it. Returns $true only if it is really foreground.
function Focus-Np {
  $h = Get-NpHandle
  if ($h -eq [IntPtr]::Zero) { return $false }
  for ($i = 0; $i -lt 8; $i++) {
    if ([WinFocus]::ForceForeground($h)) { return $true }
    Start-Sleep -Milliseconds 120
    if ([WinFocus]::GetForegroundWindow() -eq $h) { return $true }
  }
  return $false
}

function Assert-Focus {
  $h = Get-NpHandle
  return ($h -ne [IntPtr]::Zero -and [WinFocus]::GetForegroundWindow() -eq $h)
}

$global:Ser = $null
function Open-Ser($port = "COM35", $baud = 9600) {
  $global:Ser = New-Object System.IO.Ports.SerialPort($port, $baud, "None", 8, "One")
  $global:Ser.ReadTimeout = 400
  $global:Ser.NewLine = "`n"
  $global:Ser.Encoding = [System.Text.Encoding]::UTF8
  $global:Ser.Open()
  Start-Sleep -Milliseconds 250
  # Firmware local echo is ON by default; silence it so serial parsing stays clean and
  # deterministic for the programmatic harness (real users keep it on).
  $global:Ser.Write("SET ECHO OFF`n")
  Start-Sleep -Milliseconds 150
  $global:Ser.DiscardInBuffer()
}
function Close-Ser { if ($global:Ser -and $global:Ser.IsOpen) { $global:Ser.Close() } }

function Send-Line($cmd, $waitMs = 400, $maxLines = 30) {
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write($cmd + "`n")
  Start-Sleep -Milliseconds $waitMs
  $sb = New-Object System.Text.StringBuilder
  for ($i = 0; $i -lt $maxLines; $i++) { try { [void]$sb.AppendLine($global:Ser.ReadLine()) } catch { break } }
  return $sb.ToString()
}

# Send a command and block until the firmware answers OK/ERR (it prints OK only
# after typing fully completes), so readback is never premature. Returns $true on OK.
function Cmd-Wait($cmd, $timeoutMs = 20000) {
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write($cmd + "`n")
  $deadline = [Environment]::TickCount + $timeoutMs
  while ([Environment]::TickCount -lt $deadline) {
    try { $ln = $global:Ser.ReadLine() } catch { continue }
    if ($ln -match '^OK') { return $true }
    if ($ln -match '^ERR') { return $false }
  }
  return $false
}

# Read Notepad, waiting until its content length is stable across two reads
# (defends against reading mid-render). Returns the stable string.
function Read-Stable($settleMs = 150, $maxMs = 3000) {
  $prev = Read-Np
  $deadline = [Environment]::TickCount + $maxMs
  while ([Environment]::TickCount -lt $deadline) {
    Start-Sleep -Milliseconds $settleMs
    $cur = Read-Np
    if ($cur -ceq $prev) { return $cur }
    $prev = $cur
  }
  return $prev
}

# Parse STATUS into a hashtable (mode-agnostic health: typed/skipped/errors/rxdrop/aborts).
function Get-Status {
  $r = Send-Line "STATUS" 500 30
  $h = @{}
  foreach ($tok in ($r -split "\s+")) {
    if ($tok -match "^([a-z]+)=(\d+)$") { $h[$Matches[1]] = [int]$Matches[2] }
  }
  $h["_raw"] = $r
  return $h
}

# Current firmware mode string: "usb", "ps2", or "probe".
function Get-Mode {
  $r = Send-Line "INFO" 500 30
  if ($r -match "mode=(usb|ps2|probe)") { return $Matches[1] }
  return "?"
}

# Parse the PS2 counters line into a hashtable.
function Get-PS2 {
  $r = Send-Line "PS2" 500 30
  $h = @{}
  foreach ($tok in ($r -split "\s+")) {
    if ($tok -match "^([a-z_]+)=(0x[0-9a-fA-F]+|\d+)$") {
      $k = $Matches[1]; $v = $Matches[2]
      if ($v -like "0x*") { $h[$k] = [Convert]::ToInt32($v, 16) } else { $h[$k] = [int]$v }
    }
  }
  $h["_raw"] = $r
  return $h
}
