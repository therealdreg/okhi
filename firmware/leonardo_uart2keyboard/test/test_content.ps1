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

param([int]$Lines = 30, [int]$LineLen = 48, [int]$AccentReps = 40, [int]$Seed = 12345,
      [int]$Settle = 0, [int]$Press = 0, [int]$Gap = 0, [string]$Port = "COM35")
. "$PSScriptRoot\ps2lib.ps1"

$rng = New-Object System.Random($Seed)
# Strict 1:1 corpus on ES layout: letters, digits, space (interior), ES accents.
$lower = 97..122 | ForEach-Object { [char]$_ }
$upper = 65..90  | ForEach-Object { [char]$_ }
$digit = 48..57  | ForEach-Object { [char]$_ }
$acc   = @([char]0x00E1,[char]0x00E9,[char]0x00ED,[char]0x00F3,[char]0x00FA,
           [char]0x00C1,[char]0x00C9,[char]0x00CD,[char]0x00D3,[char]0x00DA,
           [char]0x00F1,[char]0x00D1)
$pool = @($lower + $upper + $digit + $acc)
$poolSp = @($pool + ' ')  # space allowed interior

function New-Line($len) {
  $sb = New-Object System.Text.StringBuilder
  for ($i = 0; $i -lt $len; $i++) {
    if ($i -eq 0 -or $i -eq $len-1) { $c = $pool[$rng.Next($pool.Count)] }   # no leading/trailing space
    else { $c = $poolSp[$rng.Next($poolSp.Count)] }
    [void]$sb.Append($c)
  }
  return $sb.ToString()
}

Open-Ser $Port
$mode = Get-Mode
if ($mode -ne "ps2" -and $mode -ne "usb") { "ABORT: board mode is '$mode' (need usb or ps2)"; Close-Ser; return }
Write-Host "MODE = $mode"
if ($Settle -gt 0) { [void](Cmd-Wait "SET SETTLE $Settle" 2000) }
if ($Press  -gt 0) { [void](Cmd-Wait "SET PRESS $Press" 2000) }
if ($Gap    -gt 0) { [void](Cmd-Wait "SET GAP $Gap" 2000) }
if ($Settle -gt 0 -or $Press -gt 0 -or $Gap -gt 0) { Write-Host "timing: settle=$Settle press=$Press gap=$Gap" }
$start = Get-Status

$totalChars = 0; $okLines = 0; $badLines = 0; $rawFails = 0; $mismatches = @()
$maxRetry = 3
$aborted = $false   # focus could not be secured: infrastructure, not a firmware failure

Write-Host "=== TEST A: $Lines random lines x $LineLen chars (byte-exact, verify+retry) ==="
for ($ln = 0; $ln -lt $Lines; $ln++) {
  $exp = New-Line $LineLen
  $pass = $false; $lastGot = ""
  for ($try = 0; $try -le $maxRetry; $try++) {
    if (-not (Focus-Np)) { "ABORT at line $ln : cannot secure Notepad focus"; $aborted = $true; break }
    # On retry, escalate to a slower, safe per-char timing profile. Windows drops the
    # occasional modifier transition when reports arrive faster than it processes them;
    # bigger press/gap/settle gives it time. Escalation converges even the worst lines.
    if ($try -gt 0) {
      [void](Cmd-Wait ("SET PRESS "  + (20 + 8 * $try))  2000)
      [void](Cmd-Wait ("SET GAP "    + (20 + 8 * $try))  2000)
      [void](Cmd-Wait ("SET SETTLE " + (30 + 12 * $try)) 2000)
    }
    [void](Clear-Np)
    [void](Cmd-Wait "TYPE $exp" 40000)
    if (-not (Assert-Focus)) { "ABORT at line $ln : focus lost during typing"; $aborted = $true; break }
    $lastGot = Read-Stable
    if ($lastGot -ceq $exp) { $pass = $true; break }
    $rawFails++   # a raw type attempt that did not match (host-side flip/drop)
  }
  if ($try -gt 0) {   # restore the run's base profile after any retry
    if ($Press -gt 0)  { [void](Cmd-Wait "SET PRESS $Press" 2000) }
    if ($Gap -gt 0)    { [void](Cmd-Wait "SET GAP $Gap" 2000) }
    if ($Settle -gt 0) { [void](Cmd-Wait "SET SETTLE $Settle" 2000) }
    if ($Press -le 0 -and $Gap -le 0 -and $Settle -le 0) { [void](Cmd-Wait "RESET" 2000) }
  }
  if ($aborted) { break }   # do not score a line we never got to type
  $totalChars += $exp.Length
  if ($pass) { $okLines++ }
  else {
    $badLines++
    $d = -1; $m = [Math]::Min($exp.Length, [int]$lastGot.Length)
    for ($k=0; $k -lt $m; $k++){ if ($exp[$k] -cne $lastGot[$k]) { $d = $k; break } }
    if ($d -lt 0 -and $exp.Length -ne $lastGot.Length) { $d = $m }
    $mismatches += [pscustomobject]@{ line=$ln; at=$d; expLen=$exp.Length; gotLen=[int]$lastGot.Length; exp=$exp; got=$lastGot }
  }
}
Write-Host ("TEST A: lines OK=$okLines BAD=$badLines  chars=$totalChars  rawTypeFails=$rawFails (auto-retried)")
if ($aborted) { Write-Host "  ABORTED early on a focus problem: infrastructure, not a firmware failure" }
foreach ($mm in ($mismatches | Select-Object -First 8)) {
  Write-Host ("  MISMATCH line $($mm.line) at $($mm.at) (expLen=$($mm.expLen) gotLen=$($mm.gotLen))")
  Write-Host ("    exp=[$($mm.exp)]")
  Write-Host ("    got=[$($mm.got)]")
}

Write-Host "=== TEST B: accent worst-case stress (aAZ + full ES set + nN pairs) x $AccentReps ==="
$accStr = ([char]0x00E1+[char]0x00C1+[char]0x00E9+[char]0x00C9+[char]0x00ED+[char]0x00CD+[char]0x00F3+[char]0x00D3+[char]0x00FA+[char]0x00DA+([char]0x00F1+[char]0x00D1)*3)
$accOk = 0; $accBad = 0; $accBadDetail = @()
for ($r = 0; $r -lt $AccentReps; $r++) {
  if (-not (Focus-Np)) { "ABORT accent at $r : cannot secure focus"; break }
  [void](Clear-Np)
  [void](Cmd-Wait "TYPE $accStr" 30000)
  if (-not (Assert-Focus)) { "ABORT accent at $r : focus lost"; break }
  $got = Read-Stable
  if ($got -ceq $accStr) { $accOk++ } else { $accBad++; if ($accBadDetail.Count -lt 6) { $accBadDetail += "rep $r got=[$got]" } }
}
Write-Host ("TEST B: reps OK=$accOk BAD=$accBad")
foreach ($d in $accBadDetail) { Write-Host "  $d" }

$end = Get-Status
Write-Host "=== health counters (mode=$mode) ==="
Write-Host ("  DELTA: typed=$($end['typed']-$start['typed']) skipped=$($end['skipped']-$start['skipped']) errors=$($end['errors']-$start['errors']) rxdrop=$($end['rxdrop']-$start['rxdrop']) aborts=$($end['aborts']-$start['aborts'])")
if ($mode -eq "ps2") {
  $p = Get-PS2
  Write-Host ("  ps2: framing=$($p['framing']) dropped=$($p['dropped']) aborts=$($p['aborts']) idle=" + ($p['_raw'] -match 'clk=1 dat=1'))
}
Close-Ser

# 0 = every line and every accent rep matched, 1 = a mismatch, 2 = aborted on infrastructure
if ($badLines -gt 0 -or $accBad -gt 0) { exit 1 }
if ($aborted -or $okLines -eq 0 -or $accOk -eq 0) { exit 2 }
exit 0
