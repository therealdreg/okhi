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

# Run the okhi-kbd hardware test suite. Documentation: ../README.md, section "Test suite".
# Prerequisites: a Notepad window open, the UART adapter on $Port, and the board reachable
# (mode usb or ps2). Do not touch the keyboard/mouse while it runs.
#
# Default: UART line handling, the settings mirror, content, edit keys and the LAYOUT test,
# a few minutes. It exits 0 if everything passed, 1 on a real failure, 2 if something could
# not be verified.
# test_layout.ps1 also needs the US keyboard layout available in Windows; it switches the
# Notepad window between Spanish and US by itself and puts it back.
# -Full   : also runs test_all.ps1, the full acceptance matrix over all nine report
#           formats plus polling and EEPROM persistence. Much longer, and it rewrites
#           the saved KRO and POLL settings (it restores KRO BOOT and POLL DEFAULT).
param([string]$Port = "COM35", [int]$Lines = 60, [int]$LineLen = 64, [int]$AccentReps = 40,
      [int]$Seed = 12345, [int]$KeyReps = 3, [switch]$Full)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$results = @()

# Every child reports 0 = passed, 1 = a real failure, 2 = could not verify (focus, missing
# layout, board not reachable). Anything else is the child dying, which counts as a failure.
function Run-Child($title, $script, $childArgs) {
  Write-Host "`n>>> $title"
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$here\$script" @childArgs
  $code = $LASTEXITCODE
  $verdict = switch ($code) { 0 { "PASS" } 1 { "FAIL" } 2 { "SKIP" } default { "FAIL" } }
  $script:results += [pscustomobject]@{ Test = $script; Verdict = $verdict; Code = $code }
}

Write-Host "======================================================================"
Write-Host " okhi-kbd hardware test suite   port=$Port   full=$($Full.IsPresent)"
Write-Host "======================================================================"

Run-Child "UART LINE HANDLING (echo default, CR/LF/CRLF/LFCR, NUL)" "test_uart.ps1" @("-Port", $Port)
Run-Child "SETTINGS MIRROR (SET, RESET, FACTORY_RESET)" "test_settings.ps1" @("-Port", $Port)
Run-Child "CONTENT + ACCENTS + SOAK" "test_content.ps1" @(
  "-Port", $Port, "-Lines", $Lines, "-LineLen", $LineLen, "-AccentReps", $AccentReps, "-Seed", $Seed)
Run-Child "EDIT KEYS + COMBOS" "test_keys.ps1" @("-Port", $Port, "-Reps", $KeyReps)
Run-Child "LAYOUT es-ES vs en-US" "test_layout.ps1" @("-Port", $Port)

if ($Full) {
  Run-Child "FULL ACCEPTANCE MATRIX (all report formats, speed, polling, persistence)" `
    "test_all.ps1" @("-Port", $Port)
} else {
  Write-Host "`n(skipping test_all.ps1; pass -Full to include it)"
  Write-Host "(test_nkro.ps1 is standalone: set the format with KRO <fmt> first, then run it)"
}

$failed = @($results | Where-Object { $_.Verdict -eq "FAIL" })
$skipped = @($results | Where-Object { $_.Verdict -eq "SKIP" })

Write-Host "`n======================================================================"
Write-Host " SUITE RESULT"
Write-Host "======================================================================"
foreach ($r in $results) {
  $colour = switch ($r.Verdict) { "PASS" { "Green" } "SKIP" { "Yellow" } default { "Red" } }
  Write-Host ("  {0,-18} {1}" -f $r.Test, $r.Verdict) -ForegroundColor $colour
}
Write-Host ""
if ($failed.Count) {
  Write-Host (" FAILED: {0} of {1}" -f $failed.Count, $results.Count) -ForegroundColor Red
  Write-Host "======================================================================"
  exit 1
}
if ($skipped.Count) {
  Write-Host (" NOT VERIFIED: {0} of {1} could not run (focus, layout or link). Nothing failed." -f `
    $skipped.Count, $results.Count) -ForegroundColor Yellow
  Write-Host "======================================================================"
  exit 2
}
Write-Host (" ALL {0} PASSED" -f $results.Count) -ForegroundColor Green
Write-Host "======================================================================"
exit 0
