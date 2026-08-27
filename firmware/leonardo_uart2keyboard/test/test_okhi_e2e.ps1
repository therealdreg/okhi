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

# END TO END test of the whole capture chain, which is the reason this board exists:
#
#   okhi-kbd  --PS/2 wire-->  okhi implant  --SPI-->  ESP  --SPIFFS-->  HTTP  -->  decoder
#
# Every burst is checked twice over, and the two halves say different things when they
# disagree, which is the point:
#   * what NOTEPAD received on this PC, so the wire and the host agree;
#   * what the IMPLANT captured and okhi's OWN decoder makes of it, lifted out of
#     webps2/index.html by okhi_decode.js so it can never drift from the shipped page.
# Notepad right and okhi wrong means the fault is in okhi. Both wrong means the board.
#
# It also checks the raw scancode stream for something no decoder can hide: every make
# must have its matching break. A missing break is bytes lost somewhere in the chain,
# and it silently corrupts every character that follows.
#
# Documentation: ../README.md, section "End to end test".
#
# SAFETY: this types on a real keyboard wire into THIS PC. It aborts rather than type
# when Notepad cannot be focused, exactly like the rest of the suite.

param(
    [string]$Port = "COM35",
    [string]$Implant = "192.168.1.77",
    [int]$Reps = 1,
    [switch]$KeepLog
)
. "$PSScriptRoot\ps2lib.ps1"

$ErrorActionPreference = "Stop"
$Node = "node"
$Page = Join-Path $PSScriptRoot "..\..\..\webps2\index.html"
$Decoder = Join-Path $PSScriptRoot "okhi_decode.js"
$Work = Join-Path $env:TEMP "okhi_e2e"
if (-not (Test-Path $Work)) { [void](New-Item -ItemType Directory -Path $Work) }

$script:pass = 0
$script:fail = 0
$script:skip = 0
function Ok($n, $d) { Write-Host "  PASS  $n" -ForegroundColor Green; if ($d) { Write-Host "        $d" }; $script:pass++ }
function No($n, $d) { Write-Host "  FAIL  $n" -ForegroundColor Red; if ($d) { Write-Host "        $d" }; $script:fail++ }
function Sk($n, $d) { Write-Host "  SKIP  $n" -ForegroundColor Yellow; if ($d) { Write-Host "        $d" }; $script:skip++ }
function Show($s) { if ($null -eq $s) { return "<null>" }; return ($s -replace "`r", "" -replace "`n", "\n") }

# ---------------------------------------------------------------- implant HTTP

function Implant-Get($path) {
    return (Invoke-WebRequest -Uri "http://$Implant$path" -TimeoutSec 15 -UseBasicParsing)
}

# key=value bodies (/stats, /versions) as a hashtable.
function Implant-Map($path) {
    $h = @{}
    foreach ($line in (Implant-Get $path).Content -split "`n") {
        if ($line -match '^\s*([a-z_0-9]+)=(.*)$') { $h[$Matches[1]] = $Matches[2].Trim() }
    }
    return $h
}

function Clear-Keylog {
    [void](Implant-Get "/keylog?clear=1")
    Start-Sleep -Milliseconds 400
}

# Download the whole log. Restarts from zero if the generation changes underneath, which
# is what the firmware bumps on a rotation or a clear, and retries an empty chunk the way
# the web page does rather than truncating.
function Get-Keylog {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        $first = Implant-Get "/keylog?from=0"
        $total = [int]$first.Headers['X-Keylog-Total']
        $gen = [int]$first.Headers['X-Keylog-Gen']
        $sb = New-Object System.Text.StringBuilder
        $off = 0
        $restart = $false
        while ($off -lt $total) {
            $chunk = Implant-Get "/keylog?from=$off"
            if ([int]$chunk.Headers['X-Keylog-Gen'] -ne $gen) { $restart = $true; break }
            $body = [string]$chunk.Content
            if ($body.Length -eq 0) {
                $retried = $false
                for ($r = 0; $r -lt 5; $r++) {
                    Start-Sleep -Milliseconds 120
                    $chunk = Implant-Get "/keylog?from=$off"
                    $body = [string]$chunk.Content
                    if ($body.Length -gt 0) { $retried = $true; break }
                }
                if (-not $retried) { break }
            }
            [void]$sb.Append($body)
            $next = [int]$chunk.Headers['X-Keylog-Next']
            if ($next -le $off) { break }
            $off = $next
        }
        if (-not $restart) { return @{ text = $sb.ToString(); total = $total; gen = $gen } }
    }
    return @{ text = ""; total = 0; gen = -1 }
}

# okhi's own decoder, run over the captured log.
function Decode-Keylog($text, $layout) {
    $f = Join-Path $Work "keylog.txt"
    [System.IO.File]::WriteAllText($f, $text, [System.Text.Encoding]::UTF8)
    $out = & $Node $Decoder $Page $f $layout 2>&1
    if ($LASTEXITCODE -ne 0) { throw "okhi_decode.js failed: $out" }
    return ($out -join "")
}

# The scancode bytes, in wire order, direction D (device to host) only.
function Scancodes($text) {
    $codes = @()
    foreach ($m in [regex]::Matches($text, 'D:0x([0-9A-Fa-f]{1,2})\s+t:')) {
        $codes += $m.Groups[1].Value.ToUpper().PadLeft(2, '0')
    }
    return , $codes
}

# Every make must have its break. Returns the offenders, empty when the stream is sane.
# Set 2: a break is F0 <code>, an extended key is E0 <code> and breaks as E0 F0 <code>.
function Unbalanced($codes) {
    $make = @{}
    $brk = @{}
    $i = 0
    while ($i -lt $codes.Count) {
        $ext = $false
        if ($codes[$i] -eq 'E0') { $ext = $true; $i++ }
        if ($i -ge $codes.Count) { break }
        if ($codes[$i] -eq 'E1') { $i += 8; continue }      # pause, fixed length, no break
        $isBreak = $false
        if ($codes[$i] -eq 'F0') { $isBreak = $true; $i++ }
        if ($i -ge $codes.Count) { break }
        $key = $(if ($ext) { "E0 " } else { "" }) + $codes[$i]
        if ($isBreak) { $brk[$key] = 1 + $(if ($brk.ContainsKey($key)) { $brk[$key] } else { 0 }) }
        else { $make[$key] = 1 + $(if ($make.ContainsKey($key)) { $make[$key] } else { 0 }) }
        $i++
    }
    $bad = @()
    foreach ($k in ($make.Keys + $brk.Keys | Sort-Object -Unique)) {
        $m = $(if ($make.ContainsKey($k)) { $make[$k] } else { 0 })
        $b = $(if ($brk.ContainsKey($k)) { $brk[$k] } else { 0 })
        if ($m -ne $b) { $bad += ("{0}: {1} makes, {2} breaks" -f $k, $m, $b) }
    }
    return , $bad
}

# ---------------------------------------------------------------- the board

function Board-Cmd($cmd, $timeoutMs = 40000) {
    $global:Ser.DiscardInBuffer()
    $global:Ser.Write($cmd + "`n")
    $deadline = [Environment]::TickCount + $timeoutMs
    while ([Environment]::TickCount -lt $deadline) {
        try { $ln = $global:Ser.ReadLine() } catch { continue }
        if ($ln -match '^(OK|ERR)') { return $ln.Trim() }
    }
    return ""
}

# One burst, captured at both ends. Returns $null if focus could not be secured, in
# which case NOTHING was typed.
function Capture($cmds, $settleSeconds = 6) {
    if (-not (Focus-Np)) { return $null }
    if (-not (Clear-Np)) { return $null }
    Clear-Keylog
    $reply = ""
    foreach ($c in $cmds) {
        $reply = Board-Cmd $c
        if ($reply -notmatch '^OK') { break }
        if (-not (Assert-Focus)) { return $null }
    }
    if (-not (Assert-Focus)) { return $null }
    $notepad = Read-Stable
    Start-Sleep -Seconds $settleSeconds
    $log = Get-Keylog
    return @{
        reply    = $reply
        notepad  = $(if ($null -eq $notepad) { "" } else { $notepad })
        raw      = $log.text
        total    = $log.total
        codes    = (Scancodes $log.text)
        decoded  = (Decode-Keylog $log.text "spanish")
    }
}

# One burst, three observations, and a VERDICT that names the guilty side.
#
# Notepad is the oracle for "was it on the wire". Windows only produces the right
# character if the right scancodes arrived, so:
#
#   notepad OK  + okhi OK    the whole chain is right
#   notepad OK  + okhi WRONG the bytes were on the bus and OKHI lost or misread them
#   notepad BAD + okhi WRONG the board never put it on the bus
#   notepad BAD + okhi OK    the wire was fine, this PC read it with the wrong layout
#
# The make/break balance sharpens the second case: a missing break with notepad correct
# is the implant dropping bytes, not the decoder misreading them.
function Case($name, $cmds, $wantNotepad, $wantDecoded) {
    $c = Capture $cmds
    if ($null -eq $c) { Sk $name "no focus on notepad, nothing was typed"; return }

    $npOk = ($c.notepad -ceq $wantNotepad)
    $okhiOk = ($c.decoded -ceq $wantDecoded)
    $bad = Unbalanced $c.codes
    $balanced = ($bad.Count -eq 0)

    if ($npOk -and $okhiOk -and $balanced) {
        Ok $name ("{0} scancodes, okhi read [{1}]" -f $c.codes.Count, (Show $c.decoded))
        return
    }

    $verdict = ""
    if ($npOk -and -not $okhiOk) {
        $verdict = "VERDICT: the host decoded it correctly, so the bytes WERE on the wire. OKHI lost or misread them."
    } elseif (-not $npOk -and -not $okhiOk) {
        $verdict = "VERDICT: both ends are wrong, so the BOARD never put the right bytes on the wire."
    } elseif (-not $npOk -and $okhiOk) {
        $verdict = "VERDICT: okhi read the wire correctly, so THIS PC decoded it wrong. Check the windows layout."
    } elseif ($npOk -and $okhiOk -and -not $balanced) {
        $verdict = "VERDICT: the text is right at both ends but the stream is not whole. OKHI dropped bytes that happened not to change the text."
    }

    $detail = @()
    $detail += ("notepad {0}: exp [{1}] got [{2}]" -f $(if ($npOk) { "ok" } else { "WRONG" }), (Show $wantNotepad), (Show $c.notepad))
    $detail += ("okhi    {0}: exp [{1}] got [{2}]" -f $(if ($okhiOk) { "ok" } else { "WRONG" }), (Show $wantDecoded), (Show $c.decoded))
    if (-not $balanced) { $detail += ("stream NOT whole: " + ($bad -join "  ")) }
    $detail += $verdict

    No $name ($detail -join "`n        ")

    # A stream that is not whole is worth keeping: it is the evidence for the bug hunt.
    $dump = Join-Path $Work ("bad_{0}_{1}.txt" -f ($name -replace '[^a-zA-Z0-9]', '_'), (Get-Date -Format "HHmmss"))
    [System.IO.File]::WriteAllText($dump, $c.raw, [System.Text.Encoding]::UTF8)
    Write-Host "        raw capture saved to $dump"
}

# Same burst, but with no claim on notepad. For keys whose effect on a text control is
# an edit rather than a character, the control's contents prove nothing about the wire.
function CaseWireOnly($name, $cmds, $wantDecoded) {
    $c = Capture $cmds
    if ($null -eq $c) { Sk $name "no focus on notepad, nothing was typed"; return }
    $bad = Unbalanced $c.codes
    $okhiOk = ($c.decoded -ceq $wantDecoded)
    if ($okhiOk -and $bad.Count -eq 0) {
        Ok $name ("{0} scancodes, okhi read [{1}]" -f $c.codes.Count, (Show $c.decoded))
        return
    }
    $detail = @(("okhi: exp [{0}] got [{1}]" -f (Show $wantDecoded), (Show $c.decoded)))
    if ($bad.Count) { $detail += ("stream NOT whole: " + ($bad -join "  ")) }
    No $name ($detail -join "`n        ")
}

# ---------------------------------------------------------------- preflight

Write-Host ""
Write-Host "======================================================================"
Write-Host " okhi END TO END capture test   board=$Port   implant=$Implant"
Write-Host "======================================================================"

if ((Get-NpHandle) -eq [IntPtr]::Zero) { Write-Host "ABORT: notepad is not open"; exit 2 }

# The notepad half of every check is only meaningful if this PC reads the wire with the
# same layout the board is typing in.
if (-not ("E2EKbd" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class E2EKbd {
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("user32.dll")] public static extern IntPtr GetKeyboardLayout(uint thread);
}
"@
}
$npThread = [E2EKbd]::GetWindowThreadProcessId((Get-NpHandle), [IntPtr]::Zero)
$hkl = [E2EKbd]::GetKeyboardLayout($npThread).ToInt64() -band 0xFFFFFFFF
$klid = ($hkl -shr 16) -band 0xFFFF
if ($klid -eq 0) { $klid = $hkl -band 0xFFFF }
if ($klid -ne 0x040A) {
    Write-Host ("ABORT: notepad's keyboard layout is 0x{0:X4}, this test needs spanish (0x040A)" -f $klid)
    exit 2
}
try { $null = & $Node --version } catch { Write-Host "ABORT: node is not on PATH"; exit 2 }
if (-not (Test-Path $Page)) { Write-Host "ABORT: cannot find $Page"; exit 2 }

try { $ver = Implant-Map "/versions" } catch { Write-Host "ABORT: implant $Implant not reachable"; exit 2 }
if ($ver['esp_variant'] -ne 'ps2') { Write-Host "ABORT: implant is variant '$($ver['esp_variant'])', this test needs ps2"; exit 2 }
if ($ver['link'] -ne 'up') { Write-Host "ABORT: implant reports link=$($ver['link'])"; exit 2 }
Write-Host ("implant: {0} | rp {1} | hw {2} | link {3}" -f $ver['esp_image'], $ver['rp_identity'], $ver['rp_hardware'], $ver['link'])

Open-Ser $Port
$info = Send-Line "INFO" 600 30
if ($info -notmatch 'mode=ps2') { Write-Host "ABORT: board is not in ps2 mode"; Close-Ser; exit 2 }
if ($info -notmatch 'layout=es-ES') { Write-Host "ABORT: board layout is not es-ES, the spanish decoder table would not match"; Close-Ser; exit 2 }

# The decoder models a keyboard whose caps lock is off and tracks 0x58 from the wire. If
# the HOST has caps on before capture starts, the two can never agree, so turn it off.
$ps2 = Send-Line "PS2" 600 30
if ($ps2 -match 'leds=0x([0-9A-Fa-f]+)') {
    $leds = [Convert]::ToInt32($Matches[1], 16)
    if ($leds -band 0x04) {
        Write-Host "host caps lock is ON, turning it off so the decoder can stay in sync"
        if (Focus-Np) {
            [void](Board-Cmd "KEY CAPSLOCK" 8000)
            Start-Sleep -Milliseconds 800
            $ps2 = Send-Line "PS2" 600 30
        }
    }
}
if ($ps2 -match 'leds=0x([0-9A-Fa-f]+)' -and ([Convert]::ToInt32($Matches[1], 16) -band 0x04)) {
    Write-Host "ABORT: could not clear host caps lock"; Close-Ser; exit 2
}

# Literal shift on the wire, which is the model okhi's decoder implements.
[void](Board-Cmd "SET CAPSFIX OFF" 5000)

if (-not $KeepLog) {
    $backup = Join-Path $Work ("keylog_before_{0}.txt" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
    $old = Get-Keylog
    [System.IO.File]::WriteAllText($backup, $old.text, [System.Text.Encoding]::UTF8)
    Write-Host ("saved the {0} bytes already in the implant to {1}" -f $old.total, $backup)
}

$before = Implant-Map "/stats"

# ---------------------------------------------------------------- the cases

for ($rep = 1; $rep -le $Reps; $rep++) {
    if ($Reps -gt 1) { Write-Host ""; Write-Host "=== round $rep of $Reps ===" }

    Write-Host ""
    Write-Host ">>> plain text"
    Case "letters and digits" @("TYPE okhi hola mundo 123") `
        "okhi hola mundo 123" "okhi[SPACE]hola[SPACE]mundo[SPACE]123"

    Write-Host ""
    Write-Host ">>> shift, which the decoder has to track as state"
    Case "mixed case" @("TYPE aA bB zZ") "aA bB zZ" "aA[SPACE]bB[SPACE]zZ"

    Write-Host ""
    Write-Host ">>> every punctuation key that is one keypress in es-ES"
    $direct = "a.,-<'+" + '!"' + [char]0x00B7 + '$%&/()=?;:_>*'
    Case "direct and shifted keys" @("TYPE $direct") $direct $direct

    Write-Host ""
    Write-Host ">>> AltGr, the modifier that rides an extended scancode"
    Case "altgr characters" @('TYPE a@#[]\|{}') "a@#[]\|{}" "a@#[]\|{}"

    Write-Host ""
    Write-Host ">>> named keys reach okhi as tokens, not characters"
    # Notepad is not an oracle here: enter, tab and backspace are editing actions, so the
    # text that ends up in the control is not a transcript of the keys. Only okhi's view
    # and the stream itself can be asserted.
    CaseWireOnly "enter, tab and backspace" @("TYPE ab", "KEY ENTER", "TYPE cd", "KEY TAB", "TYPE e", "KEY BACKSPACE") `
        "ab[ENTER]cd[TAB]e[BKSP]"

    Write-Host ""
    Write-Host ">>> dead keys: two keypresses for one character"
    $acc = "a" + [char]0x00E1 + [char]0x00F1
    $accDecoded = "a" + [char]0x00B4 + "a" + [char]0x00F1
    Case "accents" @("TYPE $acc") $acc $accDecoded
}

# ---------------------------------------------------------------- health

Write-Host ""
Write-Host ">>> the implant stayed healthy through all of it"
$after = Implant-Map "/stats"
$counters = @("spi_errors", "spi_truncated", "spi_queue_errors", "spi_proto_mismatch",
    "keylog_dropped", "ring_dropped", "http_rejected")
$moved = @()
foreach ($k in $counters) {
    $d = [int]$after[$k] - [int]$before[$k]
    if ($d -ne 0) { $moved += "$k +$d" }
}
if ($moved.Count -eq 0) { Ok "no errors, no drops, nothing truncated" (($counters | ForEach-Object { "$_=$($after[$_])" }) -join " ") }
else { No "the implant lost or rejected something" ($moved -join "  ") }

$recs = [int]$after['spi_records'] - [int]$before['spi_records']
if ($recs -gt 0) { Ok "the SPI link carried records" "spi_records +$recs" }
else { No "the SPI link carried records" "spi_records did not move" }

if ($after['link'] -eq 'up') { Ok "rp link still up" ("rp_link_age_ms={0}" -f $after['rp_link_age_ms']) }
else { No "rp link still up" ("link={0}" -f $after['link']) }

# ---------------------------------------------------------------- done

[void](Board-Cmd "SET CAPSFIX ON" 5000)
if (Focus-Np) { [void](Clear-Np) }
Close-Ser

Write-Host ""
Write-Host "======================================================================"
Write-Host " END TO END: $script:pass PASS, $script:fail FAIL, $script:skip SKIP"
Write-Host "======================================================================"

if ($script:fail -gt 0) { exit 1 }
if ($script:skip -gt 0 -or $script:pass -eq 0) { exit 2 }
exit 0
