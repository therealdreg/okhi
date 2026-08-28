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

# LAYOUT command test: proves the firmware really emits different HID usages for es-ES
# and en-US, and that both are correct against a matching Windows layout.
# Documentation: ../README.md, section "Test suite".
#
# Needs Notepad open, the board reachable on $Port in usb or ps2 mode, and BOTH the
# Spanish and the US keyboard layouts available in Windows. It switches the Windows
# layout of the Notepad window by itself and puts it back at the end.
# Non-ASCII expected text is built from code points on purpose: this file stays pure ASCII.

param([string]$Port = "COM35")
. "$PSScriptRoot\ps2lib.ps1"

if (-not ("HostKbd" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class HostKbd {
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
  [DllImport("user32.dll")] public static extern IntPtr LoadKeyboardLayout(string id, uint flags);
  [DllImport("user32.dll")] public static extern int GetKeyboardLayoutList(int n, [Out] IntPtr[] list);
  [DllImport("user32.dll")] public static extern IntPtr PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
}
"@
}

$KLID_ES = 0x040A
$KLID_US = 0x0409
$WM_INPUTLANGCHANGEREQUEST = 0x0050

function U([int[]]$cp) { return (-join ($cp | ForEach-Object { [char]$_ })) }
function Norm($s) { if ($null -eq $s) { return "" }; return ($s -replace "`r`n", "`n" -replace "`r", "`n") }
function Show($s) { return ($s -replace "`n", "\n") }

function Get-Hkls {
  $n = [HostKbd]::GetKeyboardLayoutList(0, $null)
  $a = New-Object IntPtr[] $n
  [void][HostKbd]::GetKeyboardLayoutList($n, $a)
  return $a
}

# Keyboard layout id of an HKL: high word is the layout, and a zero high word means the
# layout is the one implied by the language in the low word.
function Klid([IntPtr]$h) {
  $v = $h.ToInt64() -band 0xFFFFFFFF
  $dev = ($v -shr 16) -band 0xFFFF
  if ($dev -eq 0) { return ($v -band 0xFFFF) }
  return $dev
}

function Find-Hkl([int]$klid) {
  foreach ($h in (Get-Hkls)) { if ((Klid $h) -eq $klid) { return $h } }
  $loaded = [HostKbd]::LoadKeyboardLayout(("{0:X8}" -f $klid), 0)
  if ($loaded -ne [IntPtr]::Zero -and (Klid $loaded) -eq $klid) { return $loaded }
  return [IntPtr]::Zero
}

function Get-NpHkl {
  $h = Get-NpHandle
  if ($h -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
  $t = [HostKbd]::GetWindowThreadProcessId($h, [IntPtr]::Zero)
  return [HostKbd]::GetKeyboardLayout($t)
}

function Set-HostLayout([IntPtr]$hkl) {
  $h = Get-NpHandle
  if ($h -eq [IntPtr]::Zero -or $hkl -eq [IntPtr]::Zero) { return $false }
  for ($i = 0; $i -lt 20; $i++) {
    [void][HostKbd]::PostMessage($h, $WM_INPUTLANGCHANGEREQUEST, [IntPtr]::Zero, $hkl)
    Start-Sleep -Milliseconds 200
    if ((Get-NpHkl) -eq $hkl) { return $true }
  }
  return $false
}

# Send a command and return the OK/ERR line itself, so "OK SKIP <n>" can be inspected.
function Cmd-Reply($cmd, $timeoutMs = 40000) {
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write($cmd + "`n")
  $deadline = [Environment]::TickCount + $timeoutMs
  while ([Environment]::TickCount -lt $deadline) {
    try { $ln = $global:Ser.ReadLine() } catch { continue }
    if ($ln -match '^(OK|ERR)') { return $ln.Trim() }
  }
  return ""
}

# Type one line into an empty Notepad and give back both the firmware reply and the text
# that actually landed. Returns $null if focus could not be secured, so nothing is typed.
function Type-Read($text) {
  if (-not (Focus-Np)) { return $null }
  if (-not (Clear-Np)) { return $null }
  $reply = Cmd-Reply ("TYPE " + $text)
  if (-not (Assert-Focus)) { return $null }
  return @{ reply = $reply; text = (Norm (Read-Stable)) }
}

function Get-FwLayout {
  $r = Send-Line "LAYOUT" 400 10
  if ($r -match "layout=(\S+)") { return $Matches[1] }
  return "?"
}

$script:pass = 0
$script:fail = 0
$script:skip = 0
function Ok($name, $detail) { Write-Host "  PASS  $name" -ForegroundColor Green; if ($detail) { Write-Host "        $detail" }; $script:pass++ }
function No($name, $detail) { Write-Host "  FAIL  $name" -ForegroundColor Red; if ($detail) { Write-Host "        $detail" }; $script:fail++ }
function Sk($name, $detail) { Write-Host "  SKIP  $name" -ForegroundColor Yellow; if ($detail) { Write-Host "        $detail" }; $script:skip++ }

function Check-Typed($name, $fwLayout, [IntPtr]$hkl, $send, $expect) {
  if (-not (Set-HostLayout $hkl)) { Sk $name "could not switch the windows layout"; return }
  if (-not (Cmd-Wait "LAYOUT $fwLayout" 5000)) { No $name "board refused LAYOUT $fwLayout"; return }
  $r = Type-Read $send
  if ($null -eq $r) { Sk $name "no focus on notepad, nothing typed"; return }
  if ($r.text -ceq $expect) { Ok $name "got [$(Show $r.text)]" }
  else { No $name "exp [$(Show $expect)]  got [$(Show $r.text)]  reply [$($r.reply)]" }
}

Write-Host ""
Write-Host "======================================================================"
Write-Host " okhi-kbd LAYOUT test   port=$Port"
Write-Host "======================================================================"

if ((Get-NpHandle) -eq [IntPtr]::Zero) { Write-Host "ABORT: notepad is not open"; return }

Open-Ser $Port
$mode = Get-Mode
if ($mode -ne "ps2" -and $mode -ne "usb") { Write-Host "ABORT: board mode '$mode' (need usb or ps2)"; Close-Ser; return }

$hklEs = Find-Hkl $KLID_ES
$hklUs = Find-Hkl $KLID_US
$hklBefore = Get-NpHkl
$fwBefore = Get-FwLayout
Write-Host "MODE = $mode   firmware layout = $fwBefore"
Write-Host ("windows layouts: es=0x{0:X8} us=0x{1:X8}  current=0x{2:X8}" -f $hklEs.ToInt64(), $hklUs.ToInt64(), $hklBefore.ToInt64())
Write-Host ""

$base = Get-Status

# Every printable ASCII punctuation character. Both layouts map all of them, by different
# keys and modifiers, so a matching windows layout must reproduce the string exactly.
$punct = '!"#$%&''()*+,-./:;<=>?@[\]^_`{|}~'
$sample = "okhi 09 aZ " + $punct

Write-Host ">>> each layout against its matching windows layout"
Check-Typed "es-ES firmware + spanish windows layout" "ES" $hklEs $sample $sample
Check-Typed "en-US firmware + us windows layout"      "US" $hklUs $sample $sample

Write-Host ""
Write-Host ">>> the same text really leaves the board as different keys"
$esOnEs = $null
$usOnEs = $null
if (Set-HostLayout $hklEs) {
  if (Cmd-Wait "LAYOUT ES" 5000) { $r = Type-Read $punct; if ($r) { $esOnEs = $r.text } }
  if (Cmd-Wait "LAYOUT US" 5000) { $r = Type-Read $punct; if ($r) { $usOnEs = $r.text } }
}
if ($null -eq $esOnEs -or $null -eq $usOnEs) {
  Sk "same input, two layouts, different output" "could not type both samples"
} elseif ($esOnEs -cne $usOnEs) {
  Ok "same input, two layouts, different output" "es gives [$(Show $esOnEs)]"
  Write-Host "        us gives [$(Show $usOnEs)]"
} else {
  No "same input, two layouts, different output" "both gave [$(Show $esOnEs)]"
}

# Reading the en-US keys with a spanish windows layout must land on exactly the spanish
# characters those usages carry. This is what proves the change is in the wire, not in the host.
# ;  is usage 0x33 -> n-tilde     \  is 0x31 -> c-cedilla     @  is shift 0x1F -> double quote
# (  is shift 0x26 -> )           ?  is shift 0x38 -> _
$crossSend = ';\@(?'
$crossWant = (U 0x00F1, 0x00E7) + '")_'
Check-Typed "en-US keys read by a spanish windows layout" "US" $hklEs $crossSend $crossWant

Write-Host ""
Write-Host ">>> accented characters exist only in es-ES"
$acc = U 0x00E1, 0x00E9, 0x00ED, 0x00F3, 0x00FA, 0x00F1
Check-Typed "es-ES types the accents" "ES" $hklEs $acc $acc

if (-not (Set-HostLayout $hklUs)) { Sk "en-US drops the accents" "could not switch the windows layout" }
elseif (-not (Cmd-Wait "LAYOUT US" 5000)) { No "en-US drops the accents" "board refused LAYOUT US" }
else {
  $r = Type-Read $acc
  if ($null -eq $r) { Sk "en-US drops the accents" "no focus on notepad, nothing typed" }
  elseif ($r.reply -eq "OK SKIP 6" -and $r.text -ceq "") { Ok "en-US drops the accents" "reply [$($r.reply)], nothing typed" }
  else { No "en-US drops the accents" "exp [OK SKIP 6] and empty, got reply [$($r.reply)] text [$(Show $r.text)]" }
}

Write-Host ""
Write-Host ">>> LAYOUT DEFAULT and a bad argument"
$r = Cmd-Reply "LAYOUT DEFAULT" 5000
if ($r -match '^OK' -and (Get-FwLayout) -eq "es-ES") { Ok "LAYOUT DEFAULT goes back to es-ES" }
else { No "LAYOUT DEFAULT goes back to es-ES" "reply [$r] layout [$(Get-FwLayout)]" }
$r = Cmd-Reply "LAYOUT KLINGON" 5000
if ($r -match '^ERR usage: LAYOUT') { Ok "a bad layout name is rejected" "reply [$r]" }
else { No "a bad layout name is rejected" "reply [$r]" }

# Read the counters before the reboot phase: a reboot zeroes them, so measuring after it
# would report a meaningless delta.
$end = Get-Status

Write-Host ""
Write-Host ">>> the setting survives a reboot"
if (-not (Cmd-Wait "LAYOUT US" 5000)) { No "layout is saved in eeprom" "board refused LAYOUT US" }
else {
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write("REBOOT`n")
  Start-Sleep -Milliseconds 3500
  $global:Ser.DiscardInBuffer()
  $after = Get-FwLayout
  if ($after -eq "en-US") { Ok "layout is saved in eeprom" "after REBOOT the board reports layout=$after" }
  else { No "layout is saved in eeprom" "after REBOOT the board reports layout=$after" }
}

# Put the board and the PC back the way they were found.
[void](Cmd-Wait "LAYOUT $(if ($fwBefore -eq 'en-US') { 'US' } else { 'ES' })" 5000)
[void](Set-HostLayout $hklBefore)
[void](Focus-Np)
[void](Clear-Np)

Write-Host ""
Write-Host "======================================================================"
Write-Host " LAYOUT: $script:pass PASS, $script:fail FAIL, $script:skip SKIP"
Write-Host (" counters delta: skipped={0} errors={1} rxdrop={2} aborts={3}" -f `
  ($end['skipped'] - $base['skipped']), ($end['errors'] - $base['errors']), `
  ($end['rxdrop'] - $base['rxdrop']), ($end['aborts'] - $base['aborts']))
Write-Host " firmware layout restored to $(Get-FwLayout)"
Write-Host "======================================================================"
Close-Ser

if ($script:fail -gt 0) { exit 1 }
if ($script:skip -gt 0) { exit 2 }
exit 0
