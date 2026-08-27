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

param([int]$Reps = 3, [string]$Port = "COM35")
. "$PSScriptRoot\ps2lib.ps1"

function Norm($s) { if ($null -eq $s) { return "" }; return ($s -replace "`r`n","`n" -replace "`r","`n") }

function Run-Case($name, $cmds, $expected) {
  if (-not (Focus-Np)) { Write-Host "  [$name] ABORT: no focus"; return $false }
  [void](Clear-Np)
  foreach ($c in $cmds) {
    $global:Ser.DiscardInBuffer()
    $global:Ser.Write($c + "`n")
    # wait scaled to command; TYPE/KEY with counts need more
    Start-Sleep -Milliseconds 700
    for ($i=0; $i -lt 6; $i++){ try{ [void]$global:Ser.ReadLine() }catch{ break } }
  }
  Start-Sleep -Milliseconds 300
  if (-not (Assert-Focus)) { Write-Host "  [$name] ABORT: focus lost"; return $false }
  $got = Norm (Read-Np)
  $exp = Norm $expected
  if ($got -ceq $exp) { Write-Host "  [$name] PASS"; return $true }
  Write-Host "  [$name] FAIL  exp=[$exp] got=[$got]"
  return $false
}

Open-Ser $Port
$mode = Get-Mode
if ($mode -ne "ps2" -and $mode -ne "usb") { "ABORT: board mode '$mode' (need usb or ps2)"; Close-Ser; return }
Write-Host "MODE = $mode"
$b = Get-Status

$cases = @(
  @{ n="backspace"; c=@("TYPE abcXX","KEY BACKSPACE 2");            e="abc" },
  @{ n="tab";       c=@("TYPE a","KEY TAB","TYPE b");               e="a`tb" },
  @{ n="enter3";    c=@("TYPE l1","KEY ENTER","TYPE l2","KEY ENTER","TYPE l3"); e="l1`nl2`nl3" },
  @{ n="repeat5";   c=@("KEY a 5");                                 e="aaaaa" },
  @{ n="arrows-ins";c=@("TYPE acd","KEY LEFT 2","TYPE b");          e="abcd" },
  @{ n="combo-Sh-a";c=@("COMBO SHIFT+a");                           e="A" },
  @{ n="combo-Sh-1";c=@("COMBO SHIFT+1");                           e="!" },
  @{ n="del-fwd";   c=@("TYPE aXbc","KEY HOME","KEY RIGHT","KEY DELETE"); e="abc" }
)

$pass = 0; $fail = 0
for ($r = 0; $r -lt $Reps; $r++) {
  Write-Host "=== round $r ==="
  foreach ($tc in $cases) {
    if (Run-Case $tc.n $tc.c $tc.e) { $pass++ } else { $fail++ }
  }
}
$e = Get-Status
Write-Host "=== SUMMARY: PASS=$pass FAIL=$fail  (skipped delta=$($e['skipped']-$b['skipped']) errors delta=$($e['errors']-$b['errors']) rxdrop delta=$($e['rxdrop']-$b['rxdrop']) aborts delta=$($e['aborts']-$b['aborts'])) ==="
Close-Ser

if ($fail -gt 0) { exit 1 }
if ($pass -eq 0) { exit 2 }
exit 0
