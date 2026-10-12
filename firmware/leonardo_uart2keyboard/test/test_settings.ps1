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

# Settings test: the EEPROM image is a plain mirror of the live configuration, so any change
# is persistent the moment it is made, and RESET and FACTORY_RESET clear different amounts of it.
# Documentation: ../README.md, section "What survives a power cycle".
#
# Everything here is observable over the UART through INFO, so this test needs NO Notepad,
# no focus, no keyboard layout and no USB or PS/2 link. It types nothing.
# It finishes with FACTORY_RESET, so it always leaves the board on the compiled defaults.

param([string]$Port = "COM35")
. "$PSScriptRoot\ps2lib.ps1"

$script:pass = 0
$script:fail = 0
function Ok($name, $detail) { Write-Host "  PASS  $name" -ForegroundColor Green; if ($detail) { Write-Host "        $detail" }; $script:pass++ }
function No($name, $detail) { Write-Host "  FAIL  $name" -ForegroundColor Red; if ($detail) { Write-Host "        $detail" }; $script:fail++ }

# INFO as a hashtable: every key=value token of the four banner lines.
function Get-Info {
  $r = Send-Line "INFO" 600 30
  $h = @{}
  foreach ($m in [regex]::Matches($r, '(\w+)=([^\s]+)')) { $h[$m.Groups[1].Value] = $m.Groups[2].Value }
  $h["_raw"] = $r
  return $h
}

# A reboot restores echo to whatever is stored, and the harness wants a quiet line-based channel.
function Reboot-Board {
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write("REBOOT`n")
  Start-Sleep -Milliseconds 3800
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write("SET ECHO OFF`n")
  Start-Sleep -Milliseconds 250
  $global:Ser.DiscardInBuffer()
}

function Factory-Reset {
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write("FACTORY_RESET`n")
  Start-Sleep -Milliseconds 4200
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write("SET ECHO OFF`n")
  Start-Sleep -Milliseconds 250
  $global:Ser.DiscardInBuffer()
}

# Compare a set of INFO fields against what they should be, and name the ones that differ.
function Check($name, $info, $want) {
  $bad = @()
  foreach ($k in $want.Keys) {
    $got = $info[$k]
    if ($got -ne $want[$k]) { $bad += ("{0}={1} (want {2})" -f $k, $got, $want[$k]) }
  }
  if ($bad.Count -eq 0) { Ok $name }
  else { No $name ($bad -join "  ") }
}

$TUNED = @{ press = "44ms"; gap = "45ms"; settle = "61ms"; dead = "62ms"; jitter = "7ms";
            capsfix = "OFF"; guard = "OFF" }
$TIMING_DEFAULT = @{ press = "30ms"; gap = "30ms"; settle = "50ms"; dead = "40ms"; jitter = "5ms";
                     capsfix = "ON"; guard = "ON" }
$ALL_DEFAULT = $TIMING_DEFAULT + @{ layout = "es-ES"; kro = "boot"; speed = "low"; interval = "10" }

Write-Host ""
Write-Host "======================================================================"
Write-Host " okhi-kbd settings mirror test   port=$Port"
Write-Host "======================================================================"

Open-Ser $Port
$info = Get-Info
if (-not $info["press"]) { Write-Host "ABORT: no INFO reply from the board"; Close-Ser; exit 2 }
Write-Host "board layout=$($info['layout']) speed=$($info['speed']) kro=$($info['kro'])"

Write-Host ""
Write-Host ">>> a clean start"
Factory-Reset
Check "FACTORY_RESET leaves the compiled defaults" (Get-Info) $ALL_DEFAULT

Write-Host ""
Write-Host ">>> a SET is live immediately"
foreach ($c in @("SET PRESS 44", "SET GAP 45", "SET SETTLE 61", "SET DEAD 62", "SET JITTER 7",
                 "SET CAPSFIX OFF", "SET GUARD OFF")) { [void](Cmd-Wait $c 4000) }
Check "the five timings and two flags are live" (Get-Info) $TUNED

Write-Host ""
Write-Host ">>> and it is already persistent, nothing else to type"
Reboot-Board
Check "every SET value came back after a reboot" (Get-Info) $TUNED

Write-Host ""
Write-Host ">>> the other settings ride in the same image"
[void](Cmd-Wait "LAYOUT US" 5000)
[void](Cmd-Wait "POLL 20" 8000)
Start-Sleep -Milliseconds 3000
$global:Ser.DiscardInBuffer()
Reboot-Board
# 20 ms is above the 10 ms floor a low-speed interrupt endpoint is clamped to, so the value the
# board reports back is the one that was stored, not the clamp.
Check "layout, poll and the timings all survive together" (Get-Info) `
  ($TUNED + @{ layout = "en-US"; interval = "20" })

Write-Host ""
Write-Host ">>> RESET clears the typing knobs and nothing else"
[void](Cmd-Wait "RESET" 4000)
Check "RESET restores the timing defaults" (Get-Info) $TIMING_DEFAULT
Check "RESET leaves layout and poll alone" (Get-Info) @{ layout = "en-US"; interval = "20" }
Reboot-Board
Check "and that is persistent too" (Get-Info) ($TIMING_DEFAULT + @{ layout = "en-US"; interval = "20" })

Write-Host ""
Write-Host ">>> FACTORY_RESET wipes the whole image, not just part of it"
[void](Cmd-Wait "LAYOUT US" 5000)
[void](Cmd-Wait "SET PRESS 44" 4000)
Factory-Reset
Check "FACTORY_RESET clears every setting" (Get-Info) $ALL_DEFAULT

Write-Host ""
Write-Host "======================================================================"
Write-Host " SETTINGS MIRROR: $script:pass PASS, $script:fail FAIL"
Write-Host " board left on the compiled defaults by the final FACTORY_RESET"
Write-Host "======================================================================"
Close-Ser

if ($script:fail -gt 0) { exit 1 }
if ($script:pass -eq 0) { exit 2 }
exit 0
