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

# PS/2 CAPTURE SOAK, one adapter at a time.
#
# The question this answers is not "did it work" but "how many bytes does this adapter
# make the sniffer lose, out of how many". One number per adapter, comparable across
# adapters, with the evidence kept for every round that lost anything.
#
# GROUND TRUTH: the keyboard's own ps2_sent counter. The firmware increments it only
# after all eleven bits of a byte have been clocked out with no abort, so
#
#     lost = (bytes the keyboard put on the wire) - (D records the implant captured)
#
# is exact. No inference, no analyzer needed. ps2_aborts explains a legitimate
# discrepancy: a byte the host inhibited mid-frame put bits on the wire but was never
# completed, so the implant may or may not have seen something.
#
# Notepad is kept as a second, independent oracle: if the host decoded the text
# correctly then the bytes really were on the bus, whatever the counters say.
#
# usage:
#   .\test_ps2_soak.ps1 -Adapter "hell_amazon" -Minutes 30
#
# SAFETY: this types on a real keyboard wire into THIS PC. It aborts rather than type
# when Notepad cannot be focused.

param(
    [string]$Port = "COM35",
    [string]$Implant = "192.168.1.77",
    [string]$Adapter = "unknown",
    [int]$Minutes = 5,
    [string]$OutDir = "",
    [string]$TeraLog = "C:\Users\regue\Desktop\teraterm.log"
)
. "$PSScriptRoot\ps2lib.ps1"

$ErrorActionPreference = "Stop"
$Node = "node"
$Page = Join-Path $PSScriptRoot "..\..\..\webps2\index.html"
$Decoder = Join-Path $PSScriptRoot "okhi_decode.js"
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP ("okhi_soak\" + ($Adapter -replace '[^a-zA-Z0-9_-]', '_')) }
if (-not (Test-Path $OutDir)) { [void](New-Item -ItemType Directory -Path $OutDir -Force) }

# ---------------------------------------------------------------- implant

function Implant-Get($path) { return (Invoke-WebRequest -Uri "http://$Implant$path" -TimeoutSec 15 -UseBasicParsing) }

function Implant-Map($path) {
    $h = @{}
    foreach ($line in (Implant-Get $path).Content -split "`n") {
        if ($line -match '^\s*([a-z_0-9]+)=(.*)$') { $h[$Matches[1]] = $Matches[2].Trim() }
    }
    return $h
}

function Clear-Keylog { [void](Implant-Get "/keylog?clear=1"); Start-Sleep -Milliseconds 350 }

# Wait for the ring to reach the flash rather than sleeping a fixed amount: keylog_pending
# is how many ring entries the flusher has not written yet.
function Wait-Flushed($maxMs = 9000) {
    $deadline = [Environment]::TickCount + $maxMs
    $lastTotal = -1
    $stable = 0
    while ([Environment]::TickCount -lt $deadline) {
        $s = Implant-Map "/stats"
        $pending = [int]$s['keylog_pending']
        $total = [int]$s['keylog_size']
        if ($pending -eq 0 -and $total -eq $lastTotal) {
            $stable++
            if ($stable -ge 2) { return $true }
        } else { $stable = 0 }
        $lastTotal = $total
        Start-Sleep -Milliseconds 300
    }
    return $false
}

function Get-Keylog {
    $first = Implant-Get "/keylog?from=0"
    $total = [int]$first.Headers['X-Keylog-Total']
    $gen = [int]$first.Headers['X-Keylog-Gen']
    $sb = New-Object System.Text.StringBuilder
    $off = 0
    while ($off -lt $total) {
        $chunk = Implant-Get "/keylog?from=$off"
        if ([int]$chunk.Headers['X-Keylog-Gen'] -ne $gen) { return @{ text = ""; total = 0; torn = $true } }
        [void]$sb.Append([string]$chunk.Content)
        $next = [int]$chunk.Headers['X-Keylog-Next']
        if ($next -le $off) { break }
        $off = $next
    }
    return @{ text = $sb.ToString(); total = $total; torn = $false }
}

function Decode-Keylog($text) {
    $f = Join-Path $OutDir "_decode.txt"
    [System.IO.File]::WriteAllText($f, $text, [System.Text.Encoding]::UTF8)
    $out = & $Node $Decoder $Page $f "spanish" 2>&1
    if ($LASTEXITCODE -ne 0) { return "<decoder failed: $out>" }
    return ($out -join "")
}

function Scancodes($text) {
    $codes = @()
    foreach ($m in [regex]::Matches($text, 'D:0x([0-9A-Fa-f]{1,2})\s+t:')) {
        $codes += $m.Groups[1].Value.ToUpper().PadLeft(2, '0')
    }
    return , $codes
}

# ---------------------------------------------------------------- the RP's own view
#
# The RP prints every record it drains, right after clocking it into the SPI link, so a captured
# RP serial log is a THIRD counting point between the keyboard and the ESP:
#
#     my_spi_write_blocking(line, strlen((char *)line));
#     read_index++;
#     printf("%s", line);          <-- this is what lands in teraterm.log
#
# With it, `sent > rpsaw` means the PIO capture lost the byte and `rpsaw > esp` means the SPI hand
# off did. Full reasoning in section 3.4 of ps2adapter.md. Two caveats: that printf is a blocking
# USB CDC write on core1's hot path, so a board with a terminal attached is NOT timing-identical to
# one without, and Tera Term holds the file open, so it needs FileShare::ReadWrite.
function Log-Length {
    try { return (New-Object System.IO.FileInfo($TeraLog)).Length } catch { return -1 }
}

function Log-Slice($from) {
    if ($from -lt 0) { return "" }
    try {
        $fs = New-Object System.IO.FileStream($TeraLog, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            [void]$fs.Seek($from, [System.IO.SeekOrigin]::Begin)
            $sr = New-Object System.IO.StreamReader($fs)
            return $sr.ReadToEnd()
        } finally { $fs.Dispose() }
    } catch { return "" }
}

function Unbalanced($codes) {
    $make = @{}; $brk = @{}; $i = 0
    while ($i -lt $codes.Count) {
        $ext = $false
        if ($codes[$i] -eq 'E0') { $ext = $true; $i++ }
        if ($i -ge $codes.Count) { break }
        if ($codes[$i] -eq 'E1') { $i += 8; continue }
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
        if ($m -ne $b) { $bad += ("{0}:{1}m/{2}b" -f $k, $m, $b) }
    }
    return , $bad
}

# ---------------------------------------------------------------- board

function Board-Cmd($cmd, $timeoutMs = 60000) {
    $global:Ser.DiscardInBuffer()
    $global:Ser.Write($cmd + "`n")
    $deadline = [Environment]::TickCount + $timeoutMs
    while ([Environment]::TickCount -lt $deadline) {
        try { $ln = $global:Ser.ReadLine() } catch { continue }
        if ($ln -match '^(OK|ERR)') { return $ln.Trim() }
    }
    return ""
}

# The keyboard's own view of the wire.
function Board-PS2 {
    $r = Send-Line "PS2" 600 30
    $h = @{}
    foreach ($m in [regex]::Matches($r, '([a-z_]+)=(0x[0-9a-fA-F]+|\d+)')) {
        $v = $m.Groups[2].Value
        $h[$m.Groups[1].Value] = $(if ($v -like "0x*") { [Convert]::ToInt32($v, 16) } else { [int]$v })
    }
    return $h
}

# ---------------------------------------------------------------- corpus
#
# Each line exercises a different part of the wire. Shift and AltGr matter most: they add
# modifier makes and breaks around every character, and AltGr is the extended one.

$CORPUS = @(
    @{ n = "lower";  s = "abcdefghijklmnopqrstuvwxyz" },
    @{ n = "digits"; s = "0123456789 0123456789" },
    @{ n = "shift";  s = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" },
    @{ n = "mixed";  s = "aAbBcCdDeEfFgGhHiIjJkKlLmMnN" },
    @{ n = "punct";  s = "a.,-<'+" + '!"' + [char]0x00B7 + '$%&/()=?;:_>*' },
    @{ n = "altgr";  s = 'a@#[]\|{}' + "@#" },
    @{ n = "words";  s = "the quick brown fox jumps over the lazy dog 42 times" },
    @{ n = "burst";  s = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" }
)

# ---------------------------------------------------------------- preflight

$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
Write-Host ""
Write-Host "======================================================================"
Write-Host " PS/2 CAPTURE SOAK   adapter='$Adapter'   $Minutes min   $stamp"
Write-Host "======================================================================"

if ((Get-NpHandle) -eq [IntPtr]::Zero) { Write-Host "ABORT: notepad is not open"; exit 2 }
try { $ver = Implant-Map "/versions" } catch { Write-Host "ABORT: implant $Implant not reachable"; exit 2 }
if ($ver['esp_variant'] -ne 'ps2') { Write-Host "ABORT: implant is not the ps2 variant"; exit 2 }
if ($ver['link'] -ne 'up') { Write-Host "ABORT: implant link=$($ver['link'])"; exit 2 }

Open-Ser $Port
$info = Send-Line "INFO" 600 30
if ($info -notmatch 'mode=ps2') { Write-Host "ABORT: board is not in ps2 mode"; Close-Ser; exit 2 }
if ($info -notmatch 'layout=es-ES') { Write-Host "ABORT: board layout is not es-ES"; Close-Ser; exit 2 }
[void](Board-Cmd "SET CAPSFIX OFF" 5000)

Write-Host ("implant : {0} | rp {1} | hw {2}" -f $ver['esp_image'], $ver['rp_identity'], $ver['rp_hardware'])
Write-Host ("board   : {0}" -f (($info -split "`n" | Where-Object { $_ -match 'okhi-kbd-avr' }) -join ""))
Write-Host ("evidence: {0}" -f $OutDir)
Write-Host ""

$implantBefore = Implant-Map "/stats"
$boardBefore = Board-PS2

$rounds = 0
$sentTotal = 0
$capturedTotal = 0
$lostTotal = 0
$roundsWithLoss = 0
$roundsUnbalanced = 0
$notepadBad = 0
$decodeBad = 0
$tornDownloads = 0
$perCorpus = @{}
$log = New-Object System.Collections.ArrayList

$deadline = (Get-Date).AddMinutes($Minutes)
$ci = 0

while ((Get-Date) -lt $deadline) {
    $c = $CORPUS[$ci % $CORPUS.Count]
    $ci++

    if (-not (Focus-Np)) { Write-Host "  focus lost, stopping early (nothing was typed)"; break }
    if (-not (Clear-Np)) { Write-Host "  could not clear notepad, stopping early"; break }

    $b0 = Board-PS2
    Clear-Keylog
    $reply = Board-Cmd ("TYPE " + $c.s)
    if (-not (Assert-Focus)) { Write-Host "  focus lost mid burst, stopping"; break }
    $notepad = Read-Stable
    $b1 = Board-PS2
    [void](Wait-Flushed)

    $k = Get-Keylog
    if ($k.torn) { $tornDownloads++; continue }
    $codes = Scancodes $k.text
    $sent = $b1['sent'] - $b0['sent']
    $captured = $codes.Count
    $lost = $sent - $captured
    $bad = Unbalanced $codes
    $decoded = Decode-Keylog $k.text
    $npOk = ($notepad -ceq $c.s)

    $rounds++
    $sentTotal += $sent
    $capturedTotal += $captured
    if ($lost -ne 0) { $lostTotal += $lost; $roundsWithLoss++ }
    if ($bad.Count) { $roundsUnbalanced++ }
    if (-not $npOk) { $notepadBad++ }

    if (-not $perCorpus.ContainsKey($c.n)) { $perCorpus[$c.n] = @{ sent = 0; lost = 0; rounds = 0 } }
    $perCorpus[$c.n].sent += $sent
    $perCorpus[$c.n].lost += $lost
    $perCorpus[$c.n].rounds++

    $flag = ""
    if ($lost -ne 0) { $flag += " LOST=$lost" }
    if ($bad.Count) { $flag += " UNBALANCED[" + ($bad -join ",") + "]" }
    if (-not $npOk) { $flag += " NOTEPAD-MISMATCH" }
    if ($b1['aborts'] -ne $b0['aborts']) { $flag += (" aborts+{0}" -f ($b1['aborts'] - $b0['aborts'])) }
    if ($b1['resends'] -ne $b0['resends']) { $flag += (" resends+{0}" -f ($b1['resends'] - $b0['resends'])) }
    if ($b1['framing'] -ne $b0['framing']) { $flag += (" framing+{0}" -f ($b1['framing'] - $b0['framing'])) }

    $line = "{0,4}  {1,-7} sent={2,4} captured={3,4}{4}" -f $rounds, $c.n, $sent, $captured, $flag
    [void]$log.Add($line)
    if ($flag) {
        Write-Host "  $line" -ForegroundColor Yellow
        $ev = Join-Path $OutDir ("round{0:d4}_{1}.txt" -f $rounds, $c.n)
        [System.IO.File]::WriteAllText($ev, $k.text, [System.Text.Encoding]::UTF8)
        [System.IO.File]::AppendAllText($ev, "`n`n# sent=$sent captured=$captured lost=$lost`n# typed   =[$($c.s)]`n# notepad =[$notepad]`n# decoded =[$decoded]`n", [System.Text.Encoding]::UTF8)
    } elseif ($rounds % 10 -eq 0) {
        Write-Host ("  {0,4} rounds, {1} bytes on the wire, {2} lost" -f $rounds, $sentTotal, $lostTotal)
    }
}

# ---------------------------------------------------------------- report

$implantAfter = Implant-Map "/stats"
$boardAfter = Board-PS2
[void](Board-Cmd "SET CAPSFIX ON" 5000)
if (Focus-Np) { [void](Clear-Np) }
Close-Ser

$rate = 0.0
if ($sentTotal -gt 0) { $rate = 100.0 * $lostTotal / $sentTotal }
$oneIn = "never"
if ($lostTotal -gt 0) { $oneIn = "1 in " + [int]($sentTotal / $lostTotal) }

$rep = New-Object System.Text.StringBuilder
function Say($s) { [void]$rep.AppendLine($s); Write-Host $s }

Write-Host ""
Say "======================================================================"
Say " ADAPTER: $Adapter"
Say " started $stamp, $rounds rounds"
Say "======================================================================"
Say (" bytes the keyboard clocked out : {0}" -f $sentTotal)
Say (" records the implant captured   : {0}" -f $capturedTotal)
Say (" LOST                           : {0}   ({1:N4}%, {2})" -f $lostTotal, $rate, $oneIn)
Say ""
Say (" rounds that lost a byte        : {0} of {1}" -f $roundsWithLoss, $rounds)
Say (" rounds with an unpaired make   : {0}" -f $roundsUnbalanced)
Say (" rounds notepad disagreed       : {0}   (a mismatch here means the BOARD, not the sniffer)" -f $notepadBad)
Say (" torn downloads (log rotated)   : {0}" -f $tornDownloads)
Say ""
Say " per corpus line:"
foreach ($k in ($perCorpus.Keys | Sort-Object)) {
    $p = $perCorpus[$k]
    $pr = 0.0
    if ($p.sent -gt 0) { $pr = 100.0 * $p.lost / $p.sent }
    Say ("   {0,-7} {1,3} rounds  sent={2,6}  lost={3,4}  {4:N4}%" -f $k, $p.rounds, $p.sent, $p.lost, $pr)
}
Say ""
Say " keyboard side (these explain a legitimate gap):"
foreach ($k in @("aborts", "framing", "dropped", "resends", "cmds")) {
    Say ("   {0,-9} +{1}" -f $k, ($boardAfter[$k] - $boardBefore[$k]))
}
Say ""
Say " implant side:"
foreach ($k in @("spi_errors", "spi_truncated", "spi_queue_errors", "spi_proto_mismatch", "ring_dropped", "keylog_dropped", "http_rejected")) {
    Say ("   {0,-19} +{1}" -f $k, ([int]$implantAfter[$k] - [int]$implantBefore[$k]))
}
Say ("   link                {0}, rp_link_age_ms {1}" -f $implantAfter['link'], $implantAfter['rp_link_age_ms'])
Say "======================================================================"

$reportPath = Join-Path $OutDir ("report_{0}.txt" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
[System.IO.File]::WriteAllText($reportPath, $rep.ToString() + "`r`n" + ($log -join "`r`n"), [System.Text.Encoding]::UTF8)
Write-Host " report: $reportPath"

if ($rounds -eq 0) { exit 2 }
if ($lostTotal -ne 0 -or $notepadBad -ne 0) { exit 1 }
exit 0
