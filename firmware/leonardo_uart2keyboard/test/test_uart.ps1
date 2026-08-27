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

# UART line-handling regression test. Documentation: ../README.md, section "Test suite".
# Pure serial: it does NOT type into the PC, so it needs no Notepad, no focus and no PS/2
# link, only the UART adapter on $Port and a reachable board. It verifies the 2026-08-26
# changes: local echo ON by default, CR / LF / CRLF / LFCR all terminate a line (two-char
# pairs collapse to one), stray NUL padding is dropped, and SET ECHO OFF|ON toggles echo.
param([string]$Port = "COM35", [int]$Baud = 9600)

$ErrorActionPreference = "Stop"

$script:pass = 0
$script:fail = 0
function Check([string]$name, [bool]$cond) {
  if ($cond) { $script:pass++; Write-Host ("  PASS  " + $name) }
  else       { $script:fail++; Write-Host ("  FAIL  " + $name) }
}

$ser = New-Object System.IO.Ports.SerialPort($Port, $Baud, "None", 8, "One")
$ser.ReadTimeout = 400
$ser.Encoding = [System.Text.Encoding]::ASCII
$ser.Open()
Start-Sleep -Milliseconds 300

# Read everything the board sends for the next $ms milliseconds and return it as one string.
function DrainMs([int]$ms) {
  $sb = New-Object System.Text.StringBuilder
  $deadline = [Environment]::TickCount + $ms
  while ([Environment]::TickCount -lt $deadline) {
    Start-Sleep -Milliseconds 40
    try { [void]$sb.Append($ser.ReadExisting()) } catch {}
  }
  return $sb.ToString()
}

# Discard pending input, then send the exact bytes of $s (no terminator is added).
function SendRaw([string]$s) { $ser.DiscardInBuffer(); $ser.Write($s) }

function Pongs([string]$s) { return ([regex]::Matches($s, "PONG")).Count }

Write-Host "======================================================================"
Write-Host " okhi-kbd UART line-handling test   port=$Port"
Write-Host "======================================================================"

try {
  # Known starting state: RESET restores the compiled defaults, which include echo ON.
  SendRaw "RESET`n"
  [void](DrainMs 500)

  Write-Host "`n>>> echo default"
  SendRaw "INFO`n"
  $info = DrainMs 700
  Check "echo is ON by default (INFO reports echo=ON)" ($info -match "echo=ON")

  Write-Host "`n>>> local echo"
  SendRaw "ZZPROBE"
  $e = DrainMs 400
  Check "typed characters are echoed back live" ($e -match "ZZPROBE")
  SendRaw "`r"
  [void](DrainMs 400)

  Write-Host "`n>>> line terminators (CR / LF / CRLF / LFCR)"
  foreach ($t in @(
      @{ n = "CR";   s = "`r"   },
      @{ n = "LF";   s = "`n"   },
      @{ n = "CRLF"; s = "`r`n" },
      @{ n = "LFCR"; s = "`n`r" })) {
    SendRaw ("PING" + $t.s)
    $r = DrainMs 600
    Check ("PING + " + $t.n + " answers exactly one PONG") ((Pongs $r) -eq 1)
  }

  Write-Host "`n>>> two-char pairs collapse (no phantom blank line)"
  SendRaw "PING`r`n"
  $r = DrainMs 600
  $oks = ([regex]::Matches($r, "(?m)^OK")).Count
  Check "CRLF is one terminator (one PONG, no blank-line OK)" (((Pongs $r) -eq 1) -and ($oks -eq 0))

  Write-Host "`n>>> NUL padding is ignored"
  SendRaw ("`r" + [char]0 + "PING`r")
  $r = DrainMs 600
  Check "CR + NUL + PING parses cleanly (one PONG)" ((Pongs $r) -eq 1)

  Write-Host "`n>>> SET ECHO OFF|ON toggle"
  SendRaw "SET ECHO OFF`n"
  [void](DrainMs 400)
  SendRaw "ZZOFF"
  $off = DrainMs 400
  Check "ECHO OFF silences the echo" (-not ($off -match "ZZOFF"))
  SendRaw "`n"
  [void](DrainMs 400)
  SendRaw "SET ECHO ON`n"
  [void](DrainMs 400)
  SendRaw "ZZON"
  $on = DrainMs 400
  Check "ECHO ON restores the echo" ($on -match "ZZON")
  SendRaw "`n"
  [void](DrainMs 400)
}
finally {
  if ($ser.IsOpen) { $ser.Close() }
}

Write-Host "`n======================================================================"
Write-Host (" UART line-handling: {0} PASS, {1} FAIL" -f $script:pass, $script:fail)
Write-Host "======================================================================"
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
