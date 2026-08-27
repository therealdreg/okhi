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

# Rollover test: hold N distinct letter keys down at once and count how many the host
# actually registers. A 6KRO/boot report caps at 6 non-modifier keys; NKRO registers all.
# Run once in each mode:  KRO BOOT  -> expect 6 ;  KRO NKRO -> expect N.
param([int]$Keys = 10, [string]$Port = "COM35")
. "$PSScriptRoot\ps2lib.ps1"

$letters = @('a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t')
if ($Keys -gt $letters.Count) { $Keys = $letters.Count }

Open-Ser $Port
$mode = Get-Mode
$info = Send-Line "INFO" 600 30
$kro = if ($info -match "kro=(\w+)") { $Matches[1] } else { "?" }
Write-Host "MODE=$mode  kro=$kro  holding $Keys keys"

if (-not (Focus-Np)) { "ABORT: no Notepad focus"; Close-Ser; return }
[void](Cmd-Wait "REL" 3000)
[void](Clear-Np)

# Press (and hold) each letter key.
for ($i = 0; $i -lt $Keys; $i++) {
  $global:Ser.DiscardInBuffer()
  $global:Ser.Write("DOWN $($letters[$i])`n")
  Start-Sleep -Milliseconds 250
  for ($j=0; $j -lt 4; $j++){ try{ [void]$global:Ser.ReadLine() }catch{ break } }
}
Start-Sleep -Milliseconds 300
$got = Read-Stable
[void](Cmd-Wait "REL" 3000)   # release everything
Close-Ser

$want = ($letters[0..($Keys-1)])
$seen = @($want | Where-Object { $got -clike "*$_*" })
$missing = @($want | Where-Object { -not ($got -clike "*$_*") })
Write-Host ("registered $($seen.Count)/$Keys distinct keys")
Write-Host ("  raw notepad: [$got]")
if ($missing.Count) { Write-Host ("  missing: " + ($missing -join '')) }

# Expected rollover capacity per format.
switch ($kro) {
  "boot"       { $cap = 6 }
  "consumer"   { $cap = 6 }
  "lsconsumer" { $cap = 6 }
  "multi"      { $cap = 12 }  # 2 six-key reports
  "lsmulti"    { $cap = 12 }  # 2 six-key interfaces
  "array"      { $cap = 16 }  # ARRAY_KEYS slots
  default      { $cap = $Keys } # nkro, hybrid, hybrid2: unlimited
}
$expected = [Math]::Min($Keys, $cap)
if ($seen.Count -eq $expected) { Write-Host "RESULT: PASS (kro=$kro cap=$cap, registered $($seen.Count))"; exit 0 }
Write-Host "RESULT: FAIL (kro=$kro expected $expected, got $($seen.Count))"
exit 1
