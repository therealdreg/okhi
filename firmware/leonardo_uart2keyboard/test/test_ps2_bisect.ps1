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

# PS/2 CAPTURE BISECT: is the loss caused by what the HOST does, or by the wire?
#
# test_ps2_soak.ps1 measures 0.635% lost on the bench adapter. ps2_scope.py compare,
# same adapter and same corpus, measures 0.09%. Seven times apart, so something one
# harness does and the other does not is provoking the loss. This script answers that
# and nothing else: same adapter, same text, same counters, ONE variable changed at a
# time, and the four conditions interleaved round robin so drift hits all of them equally.
#
#   A soakish   clear the keylog over HTTP 350 ms before typing   (exactly what the soak does)
#   B quiet     clear the keylog, then 3 s of silence, then type  (what compare does)
#   C noclear   never clear at all, read the log delta instead     (isolates the SPIFFS unlink)
#   D load      quiet, then hammer /stats DURING the burst         (isolates HTTP under typing)
#
#   A vs B  does HTTP immediately before the burst cost bytes
#   B vs C  does the clear itself cost bytes (it unlinks two SPIFFS files)
#   B vs D  does HTTP concurrent with the burst cost bytes
#
# Ground truth is the same as the soak: the keyboard's own ps2_sent counter, which the
# firmware advances only after all eleven bits went out with no abort.
#
# usage:
#   .\test_ps2_bisect.ps1 -Rounds 20
#
# SAFETY: this types on a real keyboard wire into THIS PC. It aborts rather than type
# when Notepad cannot be focused.

param(
    [string]$Port = "COM35",
    [string]$Implant = "192.168.1.77",
    [int]$Rounds = 20,
    [string]$Text = "aAbBcCdDeEfFgGhHiIjJkKlLmMnN",
    [int]$QuietMs = 3000,
    [string]$OutDir = "",
    [string]$TeraLog = "C:\Users\regue\Desktop\teraterm.log",
    [string[]]$Only = @()
)
. "$PSScriptRoot\ps2lib.ps1"

$ErrorActionPreference = "Stop"
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP "okhi_bisect" }
if (-not (Test-Path $OutDir)) { [void](New-Item -ItemType Directory -Path $OutDir -Force) }

# ---------------------------------------------------------------- implant

# Retried on purpose. The load condition deliberately saturates the ESP's HTTP server, and a
# single timed out /stats there used to kill the whole run at round 8 with $ErrorActionPreference
# Stop. A transient timeout is part of the experiment, not a reason to lose the other 12 rounds.
function Implant-Get($path) {
    $last = $null
    for ($i = 0; $i -lt 4; $i++) {
        try { return (Invoke-WebRequest -Uri "http://$Implant$path" -TimeoutSec 15 -UseBasicParsing) }
        catch { $last = $_; $global:HttpRetries++; Start-Sleep -Milliseconds (250 * ($i + 1)) }
    }
    throw $last
}
$global:HttpRetries = 0

function Implant-Map($path) {
    $h = @{}
    foreach ($line in (Implant-Get $path).Content -split "`n") {
        if ($line -match '^\s*([a-z_0-9]+)=(.*)$') { $h[$Matches[1]] = $Matches[2].Trim() }
    }
    return $h
}

function Clear-Keylog { [void](Implant-Get "/keylog?clear=1") }

function Wait-Flushed($maxMs = 9000) {
    $deadline = [Environment]::TickCount + $maxMs
    $lastTotal = -1
    $stable = 0
    while ([Environment]::TickCount -lt $deadline) {
        $s = Implant-Map "/stats"
        if ([int]$s['keylog_pending'] -eq 0 -and [int]$s['keylog_size'] -eq $lastTotal) {
            $stable++
            if ($stable -ge 2) { return $true }
        } else { $stable = 0 }
        $lastTotal = [int]$s['keylog_size']
        Start-Sleep -Milliseconds 300
    }
    return $false
}

# Download from $from onwards. The whole log for the clearing conditions, only the new
# tail for the noclear one, which is what lets that condition never clear at all.
function Get-Keylog($from = 0) {
    $first = Implant-Get "/keylog?from=$from"
    $total = [int]$first.Headers['X-Keylog-Total']
    $gen = [int]$first.Headers['X-Keylog-Gen']
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append([string]$first.Content)
    $off = [int]$first.Headers['X-Keylog-Next']
    while ($off -lt $total) {
        $chunk = Implant-Get "/keylog?from=$off"
        if ([int]$chunk.Headers['X-Keylog-Gen'] -ne $gen) { return @{ text = ""; torn = $true } }
        [void]$sb.Append([string]$chunk.Content)
        $next = [int]$chunk.Headers['X-Keylog-Next']
        if ($next -le $off) { break }
        $off = $next
    }
    return @{ text = $sb.ToString(); torn = $false }
}

# The RP's own count of records it clocked into the SPI link.
#
# READ THIS BEFORE TRYING TO USE IT PER ROUND. GET /rp is a CACHED PRINT, not a live counter:
# report_packets_sent in com_rp_hw.h only re-emits after ~20M core1 loop iterations, so the value
# freezes for seconds at a time. A "poll until two reads agree" loop does NOT wait for it to catch
# up, it locks onto the stale value immediately, because a frozen counter reads identical twice.
# That was tried on 2026-08-27 and produced pure garbage: rp=0 for most rounds and rp=378 (exactly
# three rounds' worth) whenever the print finally refreshed mid round. Every per round RP delta and
# every stage attribution built on it had to be thrown away.
#
# So this is only ever called ONCE before and ONCE after the whole run, where a multi second lag
# does not matter. A per burst RP delta needs a live counter in the RP firmware, which does not
# exist yet.
function Rp-Packets {
    $body = (Implant-Get "/rp").Content
    if ($body -match 'packets sended:\s*0x([0-9a-fA-F]+)') { return [Convert]::ToInt64($Matches[1], 16) }
    return -1
}

# ---------------------------------------------------------------- the RP's own view
#
# The RP prints EVERY record it drains, right after clocking it into the SPI link:
#
#     my_spi_write_blocking(line, strlen((char *)line));
#     read_index++;
#     printf("%s", line);          <-- this is what lands in teraterm.log
#
# so with the RP serial captured to a file, counting `D:0x..` records in the slice written during
# a burst gives the one number that was missing: what the PIO captured and the RP believes it
# delivered. Against the keyboard's `sent` it isolates the PIO capture, against the ESP's
# spi_records it isolates the SPI hand off. No firmware change needed after all.
#
# TWO CAVEATS, both real. That printf is a blocking USB CDC write on core1's hot path, so a board
# with a terminal attached is NOT timing-identical to one without: the log perturbs the thing it
# measures. And the file is open in Tera Term, so it must be read with FileShare::ReadWrite.
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

function Scancodes($text) {
    $codes = @()
    foreach ($m in [regex]::Matches($text, 'D:0x([0-9A-Fa-f]{1,2})\s+t:')) {
        $codes += $m.Groups[1].Value.ToUpper().PadLeft(2, '0')
    }
    return , $codes
}

# ---------------------------------------------------------------- host load

# In process, on a runspace: a separate console would be a new foreground window and
# could steal focus mid burst, which rule 6.1 of ps2adapter.md forbids.
$global:LoadPs = $null
$global:LoadHandle = $null
$global:LoadFlag = $null
$global:LoadReqs = 0
function Start-Load($maxMs) {
    $flag = [hashtable]::Synchronized(@{ run = $true; n = 0 })
    $rs = [runspacefactory]::CreateRunspace()
    $rs.Open()
    $rs.SessionStateProxy.SetVariable("flag", $flag)
    $ps = [powershell]::Create()
    $ps.Runspace = $rs
    # The deadline is only a backstop; the flag is what normally ends it, so the hammer never
    # outlives the burst it belongs to and cannot bleed into the next condition.
    [void]$ps.AddScript({
        param($url, $maxMs)
        $wc = New-Object System.Net.WebClient
        $deadline = [Environment]::TickCount + $maxMs
        while ($flag.run -and [Environment]::TickCount -lt $deadline) {
            try { [void]$wc.DownloadString($url); $flag.n++ } catch { }
        }
    }).AddArgument("http://$Implant/stats").AddArgument($maxMs)
    $global:LoadFlag = $flag
    $global:LoadPs = $ps
    $global:LoadHandle = $ps.BeginInvoke()
}
# Ask it to stop, WAIT for it to really be gone, then let the ESP breathe. Killing the runspace
# while a request is in flight leaves the ESP mid-response and the next call times out.
function Stop-Load {
    if (-not $global:LoadPs) { return }
    if ($global:LoadFlag) { $global:LoadFlag.run = $false }
    if ($global:LoadHandle) { [void]$global:LoadHandle.AsyncWaitHandle.WaitOne(20000) }
    if ($global:LoadFlag) { $global:LoadReqs += [int]$global:LoadFlag.n }
    try { $global:LoadPs.Runspace.Close() } catch { }
    try { $global:LoadPs.Dispose() } catch { }
    $global:LoadPs = $null; $global:LoadHandle = $null; $global:LoadFlag = $null
    Start-Sleep -Milliseconds 500
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

function Board-PS2 {
    $r = Send-Line "PS2" 600 30
    $h = @{}
    foreach ($m in [regex]::Matches($r, '([a-z_]+)=(0x[0-9a-fA-F]+|\d+)')) {
        $v = $m.Groups[2].Value
        $h[$m.Groups[1].Value] = $(if ($v -like "0x*") { [Convert]::ToInt32($v, 16) } else { [int]$v })
    }
    return $h
}

# ---------------------------------------------------------------- preflight

$ALL_CONDS = @("soakish", "quiet", "noclear", "load")
# -Only narrows the run to one or more conditions. Separating the four needs hundreds of rounds
# each (see 7.5 of ps2adapter.md), but asking WHERE a byte dies does not: it only needs losses to
# accumulate, so -Only soakish -Rounds 40 answers PIO vs SPI hand off in a quarter of an hour.
$CONDS = $ALL_CONDS
if ($Only.Count) {
    $CONDS = @($ALL_CONDS | Where-Object { $Only -contains $_ })
    if (-not $CONDS.Count) { Write-Host "ABORT: -Only matched no condition, pick from: $($ALL_CONDS -join ', ')"; exit 2 }
}
$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

Write-Host ""
Write-Host "======================================================================"
Write-Host " PS/2 LOSS BISECT   $Rounds rounds x $($CONDS.Count) conditions   $stamp"
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

# The RP oracle is optional: without a live serial log the run still works, it just cannot split
# the PIO capture from the SPI hand off. Confirm it is actually being written rather than a stale
# file left over from a previous session, because a frozen log reads as "the RP captured nothing".
$logLive = $false
$l0 = Log-Length
if ($l0 -ge 0) {
    Start-Sleep -Seconds 3
    $logLive = ((Log-Length) -gt $l0)
}
if ($logLive) {
    Write-Host ("rp log  : {0} is live ({1} bytes)" -f $TeraLog, $l0)
} else {
    Write-Host ("rp log  : {0} is MISSING OR FROZEN, the rpsaw column will read -1" -f $TeraLog) -ForegroundColor Yellow
}

Write-Host ("implant : {0} | rp {1}" -f $ver['esp_image'], $ver['rp_identity'])
Write-Host ("text    : [{0}]  quiet window {1} ms" -f $Text, $QuietMs)
Write-Host ("evidence: {0}" -f $OutDir)
Write-Host ""

$implantBefore = Implant-Map "/stats"
$boardBefore = Board-PS2
$rpRunStart = Rp-Packets

$res = @{}
foreach ($c in $CONDS) { $res[$c] = @{ rounds = 0; sent = 0; captured = 0; lost = 0; lossRounds = 0; npBad = 0; rpsaw = 0; esp = 0 } }
$log = New-Object System.Collections.ArrayList
$aborted = $false

for ($r = 1; $r -le $Rounds -and -not $aborted; $r++) {
    foreach ($cond in $CONDS) {

        if (-not (Focus-Np)) { Write-Host "  focus lost, stopping (nothing was typed)"; $aborted = $true; break }
        if (-not (Clear-Np)) { Write-Host "  could not clear notepad, stopping"; $aborted = $true; break }

        $b0 = Board-PS2
        $esp0 = [int64](Implant-Map "/stats")['spi_records']
        $logFrom = Log-Length
        $from = 0

        switch ($cond) {
            "soakish" { Clear-Keylog; Start-Sleep -Milliseconds 350 }
            "quiet"   { Clear-Keylog; Start-Sleep -Milliseconds $QuietMs }
            "noclear" { $from = [int](Implant-Map "/stats")['keylog_size']; Start-Sleep -Milliseconds $QuietMs }
            "load"    { Clear-Keylog; Start-Sleep -Milliseconds $QuietMs; Start-Load 45000 }
        }

        [void](Board-Cmd ("TYPE " + $Text))
        if ($cond -eq "load") { Stop-Load }
        if (-not (Assert-Focus)) { Write-Host "  focus lost mid burst, stopping"; $aborted = $true; break }

        $notepad = Read-Stable
        $b1 = Board-PS2
        [void](Wait-Flushed)
        $esp1 = [int64](Implant-Map "/stats")['spi_records']

        $k = Get-Keylog $from
        if ($k.torn) { continue }
        $codes = Scancodes $k.text
        $sent = $b1['sent'] - $b0['sent']
        $captured = $codes.Count
        if ($sent -le 0) { continue }
        $lost = $sent - $captured

        # The four way split. Each stage should equal the one before it; the first that comes up
        # short owns the byte.
        #   sent    the keyboard clocked it onto the wire
        #   rpsaw   the RP captured it and drained it   (its own printf, from the serial log)
        #   esp     the ESP received the frame          (stats.spi_records)
        #   captured it reached flash                   (D records in the keylog)
        $espDelta = $esp1 - $esp0
        $rpSaw = -1
        if ($logFrom -ge 0) { $rpSaw = (Scancodes (Log-Slice $logFrom)).Count }
        $stage = ""
        if ($rpSaw -ge 0) {
            if ($rpSaw -lt $sent) { $stage += (" PIO-{0}" -f ($sent - $rpSaw)) }
            if ($espDelta -lt $rpSaw) { $stage += (" SPI-{0}" -f ($rpSaw - $espDelta)) }
        } elseif ($espDelta -lt $sent) {
            $stage += (" before-esp-{0}" -f ($sent - $espDelta))
        }
        if ($captured -lt $espDelta) { $stage += (" in-esp-{0}" -f ($espDelta - $captured)) }

        $res[$cond].rounds++
        $res[$cond].sent += $sent
        $res[$cond].captured += $captured
        $res[$cond].esp += $espDelta
        if ($rpSaw -ge 0) { $res[$cond].rpsaw += $rpSaw }
        $res[$cond].lost += $lost
        if ($lost -ne 0) { $res[$cond].lossRounds++ }
        if ($notepad -cne $Text) { $res[$cond].npBad++ }

        $flag = ""
        if ($lost -ne 0) { $flag += " LOST=$lost" }
        if ($notepad -cne $Text) { $flag += " NOTEPAD-MISMATCH" }
        if ($b1['aborts'] -ne $b0['aborts']) { $flag += (" aborts+{0}" -f ($b1['aborts'] - $b0['aborts'])) }
        if ($b1['resends'] -ne $b0['resends']) { $flag += (" resends+{0}" -f ($b1['resends'] - $b0['resends'])) }

        if ($stage) { $flag += " stage:$stage" }

        $line = "{0,4}  {1,-8} sent={2,4} rpsaw={3,4} esp={4,4} captured={5,4}{6}" -f $r, $cond, $sent, $rpSaw, $espDelta, $captured, $flag
        [void]$log.Add($line)
        if ($flag) {
            Write-Host "  $line" -ForegroundColor Yellow
            $ev = Join-Path $OutDir ("round{0:d4}_{1}.txt" -f $r, $cond)
            [System.IO.File]::WriteAllText($ev, $k.text, [System.Text.Encoding]::UTF8)
            [System.IO.File]::AppendAllText($ev, "`n`n# cond=$cond sent=$sent captured=$captured lost=$lost`n# typed  =[$Text]`n# notepad=[$notepad]`n", [System.Text.Encoding]::UTF8)
        }
    }
    if (-not $aborted) {
        $tot = 0; $lo = 0
        foreach ($c in $CONDS) { $tot += $res[$c].sent; $lo += $res[$c].lost }
        Write-Host ("  round {0,3} done, {1} bytes on the wire, {2} lost" -f $r, $tot, $lo)
    }
}

# ---------------------------------------------------------------- report

Stop-Load
# Give the RP's throttled print time to catch up before the last read of its counter.
Start-Sleep -Seconds 4
$rpRunEnd = Rp-Packets
$implantAfter = Implant-Map "/stats"
$boardAfter = Board-PS2
[void](Board-Cmd "SET CAPSFIX ON" 5000)
if (Focus-Np) { [void](Clear-Np) }
Close-Ser

$rep = New-Object System.Text.StringBuilder
function Say($s) { [void]$rep.AppendLine($s); Write-Host $s }

Write-Host ""
Say "======================================================================"
Say " LOSS BISECT   started $stamp"
Say " text [$Text]"
Say "======================================================================"
Say " condition  rounds     sent  captured   lost      rate   rounds w/loss"
foreach ($c in $CONDS) {
    $p = $res[$c]
    $rate = 0.0
    if ($p.sent -gt 0) { $rate = 100.0 * $p.lost / $p.sent }
    Say ("   {0,-9} {1,6} {2,8} {3,9} {4,6}  {5,8:N4}%  {6,6} " -f $c, $p.rounds, $p.sent, $p.captured, $p.lost, $rate, $p.lossRounds)
}
Say ""
$npTotal = 0
foreach ($c in $CONDS) { $npTotal += $res[$c].npBad }
Say (" rounds notepad disagreed       : {0}   (a mismatch here means the BOARD or a stray human keystroke, not the sniffer)" -f $npTotal)
Say ""
Say " where the bytes died (each stage should equal the one to its left):"
Say " condition   keyboard clocked   RP captured+drained   ESP received   reached the log"
foreach ($c in $CONDS) {
    $p = $res[$c]
    Say ("   {0,-9} {1,17} {2,21} {3,14} {4,17}" -f $c, $p.sent, $p.rpsaw, $p.esp, $p.captured)
}
Say ""
Say " RP column is the count of D records the RP printed on its serial ($TeraLog)."
Say " sent > RP means the PIO capture lost it. RP > ESP means the SPI hand off lost it."
Say ""
Say (" RP packets sended, whole run: {0} -> {1} (delta {2}). Cached print, only meaningful across the whole run, never per round." -f $rpRunStart, $rpRunEnd, ($rpRunEnd - $rpRunStart))
Say ""
Say " what each comparison answers:"
Say "   soakish vs quiet    HTTP 350 ms before the burst"
Say "   quiet   vs noclear  the SPIFFS unlink of /keylog?clear=1"
Say "   quiet   vs load     HTTP hammering during the burst"
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
Say ("   /stats hammered     {0} times during the load bursts, {1} HTTP retries in the harness" -f $global:LoadReqs, $global:HttpRetries)
Say "======================================================================"

$reportPath = Join-Path $OutDir ("bisect_{0}.txt" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
[System.IO.File]::WriteAllText($reportPath, $rep.ToString() + "`r`n" + ($log -join "`r`n"), [System.Text.Encoding]::UTF8)
Write-Host " report: $reportPath"

$anyRounds = 0
foreach ($c in $CONDS) { $anyRounds += $res[$c].rounds }
if ($anyRounds -eq 0) { exit 2 }
exit 0
