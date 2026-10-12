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

# Full acceptance test: every report format (typing byte-exact + rollover cap + media),
# edit keys, bus speed, polling rate, and EEPROM persistence. Safe focus (aborts, never leaks).
# Prereqs: a Notepad window open, board on $Port. Do not touch keyboard/mouse while it runs.
# Documentation: ../README.md, section "Test suite".
# Note: this rewrites the saved KRO, SPEED and POLL settings. It restores KRO BOOT,
# SPEED DEFAULT and POLL DEFAULT before it finishes.
param([string]$Port = "COM35", [int]$Lines = 4, [int]$LineLen = 44, [int]$RollKeys = 14, [int]$Seed = 24680)
. "$PSScriptRoot\ps2lib.ps1"

$rng = New-Object System.Random($Seed)
$lower = 97..122 | ForEach-Object { [char]$_ }; $upper = 65..90 | ForEach-Object { [char]$_ }
$digit = 48..57 | ForEach-Object { [char]$_ }
$acc = @([char]0x00E1,[char]0x00E9,[char]0x00ED,[char]0x00F3,[char]0x00FA,[char]0x00C1,[char]0x00C9,[char]0x00CD,[char]0x00D3,[char]0x00DA,[char]0x00F1,[char]0x00D1)
$pool = @($lower+$upper+$digit+$acc); $poolSp = @($pool+' ')
function New-Line($len){ $sb=New-Object System.Text.StringBuilder; for($i=0;$i -lt $len;$i++){ if($i -eq 0 -or $i -eq $len-1){$c=$pool[$rng.Next($pool.Count)]}else{$c=$poolSp[$rng.Next($poolSp.Count)]}; [void]$sb.Append($c)}; return $sb.ToString() }
$accStr = ([char]0x00E1+[char]0x00C1+[char]0x00E9+[char]0x00C9+[char]0x00F3+[char]0x00D3+([char]0x00F1+[char]0x00D1)*3)
$letters = @('a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t')

$results = @()
function Rec($area, $name, $ok, $note="") { $script:results += [pscustomobject]@{ area=$area; name=$name; ok=$ok; note=$note } }

function Wait-Ready($timeoutMs=8000) {
  $deadline = [Environment]::TickCount + $timeoutMs
  while ([Environment]::TickCount -lt $deadline) {
    $r = Send-Line "INFO" 500 30
    if ($r -match "usb=ready") { return $true }
    Start-Sleep -Milliseconds 300
  }
  return $false
}
function Type-Verify($exp, $retries=2) {
  for ($t=0; $t -le $retries; $t++) {
    if (-not (Focus-Np)) { return "SKIP" }
    [void](Clear-Np)
    if ($t -gt 0) { [void](Cmd-Wait ("SET PRESS "+(20+8*$t)) 2000); [void](Cmd-Wait ("SET GAP "+(20+8*$t)) 2000); [void](Cmd-Wait ("SET SETTLE "+(30+12*$t)) 2000) }
    [void](Cmd-Wait "TYPE $exp" 40000)
    if (-not (Assert-Focus)) { if ($t -gt 0){[void](Cmd-Wait "RESET" 2000)}; return "SKIP" }
    $got = Read-Stable
    if ($got -ceq $exp) { if ($t -gt 0){[void](Cmd-Wait "RESET" 2000)}; return "PASS" }
  }
  [void](Cmd-Wait "RESET" 2000)
  return "FAIL"
}
function Roll-Test($hold, $cap) {
  if (-not (Focus-Np)) { return @{r="SKIP"} }
  [void](Cmd-Wait "REL" 3000); [void](Clear-Np)
  for ($i=0;$i -lt $hold;$i++){ $global:Ser.DiscardInBuffer(); $global:Ser.Write("DOWN $($letters[$i])`n"); Start-Sleep -Milliseconds 200; for($j=0;$j -lt 4;$j++){try{[void]$global:Ser.ReadLine()}catch{break}} }
  Start-Sleep -Milliseconds 300
  $got = Read-Stable
  [void](Cmd-Wait "REL" 3000)
  if (-not (Assert-Focus)) { return @{r="SKIP"} }
  $want = $letters[0..($hold-1)]
  $seen = @($want | Where-Object { $got -clike "*$_*" }).Count
  $exp = [Math]::Min($hold, $cap)
  return @{ r = $(if($seen -eq $exp){"PASS"}else{"FAIL"}); seen=$seen; exp=$exp }
}

$formats = @(
  @{k="BOOT";       cap=6},
  @{k="ARRAY";      cap=16},
  @{k="MULTI";      cap=12},
  @{k="NKRO";       cap=$RollKeys},
  @{k="HYBRID";     cap=$RollKeys},
  @{k="HYBRID2";    cap=$RollKeys},
  @{k="CONSUMER";   cap=6;  media=$true},
  @{k="LSMULTI";    cap=12},
  @{k="LSCONSUMER"; cap=6;  media=$true}
)

Open-Ser $Port
Write-Host "================ FORMAT MATRIX ================"
foreach ($f in $formats) {
  Write-Host "`n--- KRO $($f.k) ---"
  [void](Cmd-Wait "KRO $($f.k)" 5000)
  Start-Sleep -Milliseconds 3500
  $ready = Wait-Ready
  $info = Send-Line "INFO" 600 30
  $kro = if ($info -match "kro=(\w+)") { $Matches[1] } else { "?" }
  $spd = if ($info -match "speed=(\w+)") { $Matches[1] } else { "?" }
  Write-Host "   enumerated kro=$kro speed=$spd ready=$ready"
  Rec "enum" $f.k $(if($ready -and $kro -eq $f.k.ToLower()){"PASS"}else{"FAIL"}) "kro=$kro speed=$spd"

  # typing: random lines + one accent line
  $s0 = Get-Status
  $tp = "PASS"; $bad = 0
  for ($ln=0; $ln -lt $Lines; $ln++) { $r = Type-Verify (New-Line $LineLen); if ($r -eq "FAIL"){$tp="FAIL";$bad++} elseif ($r -eq "SKIP" -and $tp -ne "FAIL"){$tp="SKIP"} }
  $ra = Type-Verify $accStr; if ($ra -eq "FAIL"){$tp="FAIL"} elseif ($ra -eq "SKIP" -and $tp -ne "FAIL"){$tp="SKIP"}
  Write-Host "   typing: $tp"
  Rec "type" $f.k $tp "badLines=$bad accents=$ra"

  # rollover
  $rr = Roll-Test $RollKeys $f.cap
  Write-Host "   rollover: $($rr.r) (seen=$($rr.seen)/exp=$($rr.exp))"
  Rec "roll" $f.k $rr.r "seen=$($rr.seen) exp=$($rr.exp)"

  # media keys
  if ($f.media) {
    $m1 = (Send-Line "CONSUMER VOLUP" 600 6); $m2 = (Send-Line "CONSUMER VOLDN" 600 6)
    $mok = ($m1 -match "OK") -and ($m2 -match "OK")
    Write-Host "   media: $(if($mok){'PASS'}else{'FAIL'})"
    Rec "media" $f.k $(if($mok){"PASS"}else{"FAIL"})
  }
  $s1 = Get-Status
  # Reliability = no lost keystrokes / no UART drops. Rejected DOWN overflow (cmderr) is expected
  # and legitimate on 6-key formats, so it is reported but not scored.
  $hz = "rxdrop=$($s1.rxdrop-$s0.rxdrop) skipped=$($s1.skipped-$s0.skipped) cmderr=$($s1.errors-$s0.errors)"
  Write-Host "   health delta: $hz"
  Rec "health" $f.k $(if(($s1.rxdrop -eq $s0.rxdrop) -and ($s1.skipped -eq $s0.skipped)){"PASS"}else{"FAIL"}) $hz
}

# restore a fast, safe typing profile and BOOT for the remaining tests
[void](Cmd-Wait "KRO BOOT" 5000); Start-Sleep -Milliseconds 2500; [void](Wait-Ready)

Write-Host "`n================ EDIT KEYS (boot) ================"
$cases = @(
  @{n="backspace"; c=@("TYPE abcXX","KEY BACKSPACE 2"); e="abc"},
  @{n="tab";       c=@("TYPE a","KEY TAB","TYPE b");    e="a`tb"},
  @{n="enter";     c=@("TYPE l1","KEY ENTER","TYPE l2");e="l1`nl2"},
  @{n="repeat";    c=@("KEY a 5");                      e="aaaaa"},
  @{n="combo";     c=@("COMBO SHIFT+a");                e="A"}
)
foreach ($tc in $cases) {
  if (-not (Focus-Np)) { Rec "keys" $tc.n "SKIP"; continue }
  [void](Clear-Np)
  foreach ($c in $tc.c){ [void](Cmd-Wait $c 8000) }
  Start-Sleep -Milliseconds 300
  $got = (Read-Stable) -replace "`r`n","`n" -replace "`r","`n"
  $exp = $tc.e -replace "`r`n","`n" -replace "`r","`n"
  $ok = if (-not (Assert-Focus)) { "SKIP" } elseif ($got -ceq $exp) { "PASS" } else { "FAIL" }
  Write-Host "   [$($tc.n)] $ok"
  Rec "keys" $tc.n $ok
}

Write-Host "`n================ BUS SPEED ================"
function Get-Speed { $i = Send-Line "INFO" 600 30; if ($i -match "speed=(\w+)") { return $Matches[1] } else { return "?" } }
function Set-Kro($k) { [void](Cmd-Wait "KRO $k" 5000); Start-Sleep -Milliseconds 2500; [void](Wait-Ready) }
function Set-Speed($s) { [void](Cmd-Wait "SPEED $s" 4000); Start-Sleep -Milliseconds 2000; [void](Wait-Ready) }

# Regression: a report wider than 8 bytes needs a full-speed endpoint, so SPEED LOW must
# not be able to force low speed while such a format is active (it would enumerate a
# 16-byte endpoint on a low-speed bus, which is out of spec).
Set-Kro "NKRO"; Set-Speed "LOW"
$sp = Get-Speed
Write-Host "   KRO NKRO + SPEED LOW  -> speed=$sp $(if($sp -eq 'full'){'PASS'}else{'FAIL'})"
Rec "speed" "wide-full" $(if($sp -eq "full"){"PASS"}else{"FAIL"}) "speed=$sp"

# A narrow format must honour the request in both directions.
Set-Kro "BOOT"; Set-Speed "LOW"
$sp = Get-Speed
Write-Host "   KRO BOOT + SPEED LOW  -> speed=$sp $(if($sp -eq 'low'){'PASS'}else{'FAIL'})"
Rec "speed" "boot-low" $(if($sp -eq "low"){"PASS"}else{"FAIL"}) "speed=$sp"

Set-Speed "FULL"
$sp = Get-Speed
Write-Host "   KRO BOOT + SPEED FULL -> speed=$sp $(if($sp -eq 'full'){'PASS'}else{'FAIL'})"
Rec "speed" "boot-full" $(if($sp -eq "full"){"PASS"}else{"FAIL"}) "speed=$sp"
Set-Speed "DEFAULT"

Write-Host "`n================ POLLING RATE ================"
[void](Cmd-Wait "KRO NKRO" 5000); Start-Sleep -Milliseconds 2500; [void](Wait-Ready)
foreach ($p in @(1,2,8)) {
  [void](Cmd-Wait "POLL $p" 4000); Start-Sleep -Milliseconds 2000; [void](Wait-Ready)
  $info = Send-Line "INFO" 600 30
  $iv = if ($info -match "interval=(\d+)") { $Matches[1] } else { "?" }
  $ok = ($iv -eq "$p")
  Write-Host "   POLL $p -> interval=$iv $(if($ok){'PASS'}else{'FAIL'})"
  Rec "poll" "POLL$p" $(if($ok){"PASS"}else{"FAIL"}) "interval=$iv"
}
[void](Cmd-Wait "POLL DEFAULT" 4000); Start-Sleep -Milliseconds 1500

Write-Host "`n================ PERSISTENCE (reboot) ================"
[void](Cmd-Wait "KRO HYBRID" 5000); Start-Sleep -Milliseconds 1500
$global:Ser.DiscardInBuffer(); $global:Ser.Write("REBOOT`n"); Start-Sleep -Milliseconds 3000; $global:Ser.DiscardInBuffer()
[void](Wait-Ready)
$info = Send-Line "INFO" 600 30
$pk = ($info -match "kro=hybrid")
Write-Host "   KRO HYBRID -> REBOOT -> kro=$(if($info -match 'kro=(\w+)'){$Matches[1]}) $(if($pk){'PASS'}else{'FAIL'})"
Rec "persist" "kro" $(if($pk){"PASS"}else{"FAIL"})
[void](Cmd-Wait "KRO BOOT" 5000); Start-Sleep -Milliseconds 1500
$global:Ser.DiscardInBuffer(); $global:Ser.Write("REBOOT`n"); Start-Sleep -Milliseconds 3000; $global:Ser.DiscardInBuffer()
[void](Wait-Ready)

Close-Ser

Write-Host "`n================ SUMMARY ================"
$pass = @($results | Where-Object { $_.ok -eq "PASS" }).Count
$fail = @($results | Where-Object { $_.ok -eq "FAIL" })
$skip = @($results | Where-Object { $_.ok -eq "SKIP" })
$results | ForEach-Object { "{0,-8} {1,-12} {2,-5} {3}" -f $_.area,$_.name,$_.ok,$_.note }
Write-Host ("`nTOTAL: PASS={0}  FAIL={1}  SKIP={2}" -f $pass, $fail.Count, $skip.Count)
if ($fail.Count) { Write-Host "FAILURES:"; $fail | ForEach-Object { "  {0}/{1}: {2}" -f $_.area,$_.name,$_.note } }
if ($skip.Count) { Write-Host "SKIPPED (focus/infra, not a firmware fault): $($skip.Count)" }

if ($fail.Count) { exit 1 }
if ($skip.Count -or $pass -eq 0) { exit 2 }
exit 0
