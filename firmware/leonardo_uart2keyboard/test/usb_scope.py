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

# The USB low speed bench: the wire (Saleae Logic 2, CH0 = D+, CH1 = D-) against what the implant's
# RP2040 captured (its serial log), with okhi-kbd as the keyboard. The USB twin of ps2_scope.py.
#
# usage:
#   python usb_scope.py check [ip]               whole bench, either variant: ports, RP log, keyboard,
#                                                analyzer (judged by the implant's esp_variant), SWD, implant
#   python usb_scope.py channels                 all 8 analyzer channels, digital and analog: what each
#                                                carries, and whether CH0/CH1 are right for the bus
#                                                (PS/2: okhi-kbd sends BAT frames, nothing is typed)
#   python usb_scope.py clock out|meter|off [gate_ms target_hz [ref]]
#                                                crystal calibration modes: ask for one, wait for the RP to
#                                                reboot into it, print its console; out also measures GPIO21
#                                                with the analyzer. Stops capture until "clock off"
#   python usb_scope.py idle [seconds]           capture an idle bus and summarise it
#   python usb_scope.py type "<text>" [port]     type into the bench Notepad tab while capturing and while
#                                                a simulated browser polls the implant, then every oracle:
#                                                wire, RP log, ESP /stats, live view (/buffer), keylog
#                                                tail, okhi-kbd's own counter, Notepad, latency
#   python usb_scope.py keys "<cmd>;<cmd>" "<web>" ["<notepad>"|-]
#                                                same, for any okhi-kbd typing commands (DOWN/UP to
#                                                overlap keys...); takes \t \n escapes; ends in REL
#   python usb_scope.py soak [lines] [seed] [press gap]
#                                                long reproducible typing run (190 chars a line), optionally
#                                                faster; prints a VERDICT that separates the capture chain,
#                                                what the web page shows and the host (Notepad)
#   python usb_scope.py content                  every kind of normal key once (symbols, AltGr, dead keys,
#                                                edit keys, keypad, CapsLock, Ctrl, en-US)
#   python usb_scope.py matrix [lines]           every low speed format x polling x typing speed, ~40 min
#   python usb_scope.py repeat [ms]              a keyboard that resends its unchanged report every <ms>
#                                                (default 4: every poll), each low speed format; needs
#                                                okhi-kbd's IDLE command
#   python usb_scope.py fullspeed                a full speed keyboard: nothing forwarded, nothing hangs,
#                                                the host still types, low speed capture exact afterwards
#   python usb_scope.py reenum [port]            trigger on okhi-kbd's REENUM detach, capture the whole
#                                                enumeration, wire vs RP log
#   python usb_scope.py analog [seconds]         real D+/D- voltages, edge times, crossover
#   python usb_scope.py decode <dir|file.sal>    re-decode a saved capture, no hardware
#   python usb_scope.py variants                 the web decoder against how real keyboards differ from
#                                                okhi-kbd (slot holes, phantom state, idle repeats...), no hardware
#
# Rules this bench paid for (2026-10-10):
#   - decode at 50 MS/s. At 500 MS/s the Saleae USB analyzer turns edge ringing into Error rows.
#   - a pulse trigger needs BOTH min and max width; min alone never fires.
#   - every capture has a deadline and is stopped on any failure: an orphan capture locks Logic 2
#     ("Cannot switch sessions while recording") until it is stopped.
#   - packet decode is polarity blind (NRZI), so it can NOT tell D+ from D-. The idle level and the
#     keep-alive width (1.33 us right, ~1 ms "keep alive" when swapped) can.
#   - the RP prints a capture buffer only every ~4 s at idle, so wait for two new "Total:" lines
#     before reading what it captured.

import csv
import collections
import difflib
import os
import re
import subprocess
import sys
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ps2_scope as ps  # noqa: E402  Notepad bench guard and Saleae analog reader, shared

RATE = 50_000_000
THRESHOLD = 3.3
# Threshold chatter on the slow low speed edges, far below the 667 ns bit: without it a long capture
# misread about one bit in 10000 packets (the analyzer's own CRC flagged every one).
GLITCH_S = 150e-9
LOG = os.environ.get("OKHI_RP_LOG", r"C:\Users\regue\Desktop\teraterm.log")
OUTROOT = os.path.join(os.environ.get("TEMP", "."), "okhi_usbscope")
OPENOCD = os.environ.get("OKHI_OPENOCD", r"C:\Users\regue\.pico-sdk\openocd\0.12.0+dev")
KBD_PORT = os.environ.get("OKHI_KBD_PORT", "COM35")
PORT_ROLES = {(0x0403, 0x6001): "okhi-kbd UART (FTDI)",
              (0x10C4, 0xEA60): "implant ESP32-C2 flashing (CP210x)",
              (0x2E8A, 0x000C): "Pi debug probe: SWD + RP2040 log UART (teraterm)"}


# ------------------------------------------------------------------ analyzer

def _usb_settings(level):
    return {"D+": 0, "D-": 1, "USB bit-rate": "Low speed (1.5 Mbps)", "USB decode level": level}


def _export(c, out):
    from saleae import automation
    c.export_raw_data_csv(directory=out, digital_channels=[0, 1])
    for level, name in (("Packets", "packets.csv"), ("Control transfers", "control.csv"),
                        ("Signals", "signals.csv")):
        a = c.add_analyzer("USB LS and FS", label=level, settings=_usb_settings(level))
        c.export_data_table(filepath=os.path.join(out, name), analyzers=[
            automation.DataTableExportConfiguration(a, automation.RadixType.HEXADECIMAL)])


def capture(out, seconds=None, trigger=None, during=None, deadline=60):
    """Timed (seconds), triggered (dict for DigitalTriggerCaptureMode) or, with neither, manual:
    recording lasts exactly as long as `during`. Never leaves a capture running in Logic 2."""
    from saleae import automation
    os.makedirs(out, exist_ok=True)
    with automation.Manager.connect(port=10430, connect_timeout_seconds=10) as m:
        dev = [d for d in m.get_devices() if not d.is_simulation][0]
        cfg = automation.LogicDeviceConfiguration(enabled_digital_channels=[0, 1], digital_sample_rate=RATE,
                                                  digital_threshold_volts=THRESHOLD, glitch_filters=[
                                                      automation.GlitchFilterEntry(ch, GLITCH_S) for ch in (0, 1)])
        if trigger:
            mode = automation.DigitalTriggerCaptureMode(**trigger)
        elif seconds:
            mode = automation.TimedCaptureMode(duration_seconds=seconds)
        else:
            mode = automation.ManualCaptureMode()
        with m.start_capture(device_id=dev.device_id, device_configuration=cfg,
                             capture_configuration=automation.CaptureConfiguration(capture_mode=mode)) as c:
            try:
                if during:
                    time.sleep(0.3)
                    during()
            except BaseException:
                c.stop()
                raise
            if not trigger and not seconds:
                c.stop()
            else:
                th = threading.Thread(target=c.wait, daemon=True)
                th.start()
                th.join(deadline)
                if th.is_alive():
                    c.stop()
                    raise RuntimeError("capture did not finish within %d s, stopped it" % deadline)
            _export(c, out)
            c.save_capture(filepath=os.path.join(out, "capture.sal"))
    return out


def load(path):
    from saleae import automation
    if os.path.isdir(path):
        return path
    out = os.path.splitext(path)[0] + "_dec"
    os.makedirs(out, exist_ok=True)
    with automation.Manager.connect(port=10430, connect_timeout_seconds=10) as m:
        with m.load_capture(path) as c:
            _export(c, out)
    return out


# ------------------------------------------------------------------ decoding

Packet = collections.namedtuple("Packet", "t pid addr ep data crc_ok")
Txn = collections.namedtuple("Txn", "t token addr ep dpid data hs crc")


def packets(capdir):
    """Packets-level table grouped SYNC..EOP. Returns (packets, decoder errors, keep-alives)."""
    out, cur, errors, keepalive = [], None, [], []
    for r in csv.DictReader(open(os.path.join(capdir, "packets.csv"), newline="")):
        v, t = r["value"], float(r["start_time"])
        if v == "Keep alive":
            keepalive.append((t, float(r["duration"])))
            continue
        if "error" in v.lower():
            errors.append((t, v))
        if v == "SYNC":
            cur = {"t": t, "pid": None, "addr": None, "ep": None, "data": [], "crc": None}
        elif cur is None:
            continue
        elif v == "EOP":
            out.append(Packet(cur["t"], cur["pid"], cur["addr"], cur["ep"], bytes(cur["data"]), cur["crc"]))
            cur = None
        elif v.startswith("PID "):
            cur["pid"] = v[4:]
        elif v.startswith("Address="):
            m = re.match(r"Address=(0x[0-9A-Fa-f]+) Endpoint=(0x[0-9A-Fa-f]+)", v)
            cur["addr"], cur["ep"] = int(m.group(1), 16), int(m.group(2), 16)
        elif v.startswith("Byte "):
            cur["data"].append(int(v[5:], 16))
        elif v.startswith("CRC"):
            cur["crc"] = v.startswith("CRC OK")
    return out, errors, keepalive


def transactions(pkts):
    """token [+ DATAx] [+ handshake]. A token cut by the end of the recording is dropped."""
    txns, i = [], 0
    while i < len(pkts):
        p = pkts[i]
        if p.pid not in ("IN", "OUT", "SETUP"):
            txns.append(Txn(p.t, p.pid, None, None, None, None, None, None))
            i += 1
            continue
        dpid = data = hs = crc = None
        j = i + 1
        if j < len(pkts) and (pkts[j].pid or "").startswith("DATA"):
            dpid, data, crc = pkts[j].pid, pkts[j].data, pkts[j].crc_ok
            j += 1
        if j < len(pkts) and pkts[j].pid in ("ACK", "NAK", "STALL"):
            hs = pkts[j].pid
            j += 1
        txns.append(Txn(p.t, p.pid, p.addr, p.ep, dpid, data, hs, crc))
        i = j
    if txns and txns[-1].token in ("IN", "OUT", "SETUP") and not txns[-1].dpid and not txns[-1].hs:
        txns.pop()
    return txns


def long_se0(capdir):
    return [(float(r["start_time"]), float(r["duration"]))
            for r in csv.DictReader(open(os.path.join(capdir, "signals.csv"), newline=""))
            if r["value"] == "SE0" and float(r["duration"]) > 2.5e-6]


def setups(capdir):
    lines, cur = [], None
    for r in csv.DictReader(open(os.path.join(capdir, "control.csv"), newline="")):
        v = r["value"]
        if v.startswith("bmRequestType="):
            cur = [float(r["start_time"]), v.split(" ")[0]]
        elif cur is not None and v.startswith(("bRequest=", "wValue=", "wIndex=", "wLength=")):
            cur.append(v)
            if v.startswith("wLength="):
                lines.append(cur)
                cur = None
    return lines


def in_data(txns):
    """Every DATAx a device returned to an IN: (t, addr, ep, DATA pid, bytes, handshake)."""
    return [(t.t, t.addr, t.ep, t.dpid, t.data, t.hs) for t in txns if t.token == "IN" and t.dpid]


def summary(capdir, verbose=True):
    pkts, errors, ka = packets(capdir)
    txns = transactions(pkts)
    print("capture %s" % capdir)
    kw = sorted(d for _, d in ka)
    print("  packets %d, keep-alive %d (median %.2f us, expect ~1.33-1.46), decoder errors %d, CRC bad %d"
          % (len(pkts), len(ka), 1e6 * kw[len(kw) // 2] if kw else 0, len(errors),
             sum(1 for p in pkts if p.crc_ok is False)))
    for t, d in long_se0(capdir):
        print("  long SE0 at %.4f s, %.2f ms (%s)" % (t, d * 1e3, "detach" if d > 0.05 else "bus reset"))
    per = collections.defaultdict(collections.Counter)
    for t in txns:
        if t.token in ("IN", "OUT", "SETUP"):
            per[(t.token, t.addr, t.ep)][t.hs or ("DATA, no handshake" if t.dpid else "no reply")] += 1
    for k in sorted(per, key=str):
        print("  %-5s addr %3s ep %s: %s" % (k[0], k[1], k[2], dict(per[k])))
    for ep in sorted({t.ep for t in txns if t.token == "IN" and t.ep}):
        polls = [t.t for t in txns if t.token == "IN" and t.ep == ep]
        if len(polls) > 2:
            g = sorted(b - a for a, b in zip(polls, polls[1:]))
            print("  EP%d poll interval median %.2f ms (min %.2f, max %.2f)"
                  % (ep, 1e3 * g[len(g) // 2], 1e3 * g[0], 1e3 * g[-1]))
    sl = setups(capdir)
    if sl:
        print("  %d SETUP" % len(sl))
        for s in sl if verbose else []:
            print("    %.4f %s" % (s[0], " ".join(s[1:])))
    reps = in_data(txns)
    intr = [r for r in reps if r[2] != 0]
    print("  IN data: %d on EP0 (control), %d on interrupt endpoints" % (len(reps) - len(intr), len(intr)))
    last = {}
    for t, a, e, dp, d, hs in intr:
        if verbose:
            print("    %.6f addr %d ep %d %s %s %s" % (t, a, e, dp, d.hex(" "), hs))
        if last.get((a, e)) == (dp, "ACK"):
            print("    !! %s repeated after an ACK at %.6f addr %d ep %d" % (dp, t, a, e))
        last[(a, e)] = (dp, hs)
        if hs != "ACK":
            print("    !! report not ACKed at %.6f (%s): the device will resend it" % (t, hs))
    for t, v in errors[:10]:
        print("  !! decoder: %.6f %s" % (t, v))
    return txns


# ------------------------------------------------------------------ RP log

RP_REC = re.compile(r"IN: 0x([0-9a-f]+)/0x([0-9a-f]+)((?: 0x[0-9a-f]+){8}| same);")
RP_LINE = re.compile(rb"IN: 0x[0-9a-f]+/0x[0-9a-f]+(?: 0x[0-9a-f]+){8};")


def rp_records(text, before=""):
    """(addr, ep, bytes, shipped) per report the RP printed. The RP ships a report only when it differs
    from the last one on its endpoint and logs a repeat as "IN: a/e same;", whose bytes are those of the
    previous report there: looked up in `before` (the log ahead of `text`) for a repeat that opens
    `text`, None when not even that has it."""
    last, out = {}, []
    for m in RP_REC.finditer(before + text):
        key = (int(m.group(1), 16), int(m.group(2), 16))
        shipped = m.group(3) != " same"
        if shipped:
            last[key] = bytes(int(x, 16) for x in m.group(3).split())
        if m.start() >= len(before):
            out.append(key + (last.get(key), shipped))
    return out


def rp_shipped(text):
    """The records the RP shipped to the ESP, as the lines it printed."""
    return [m.group(0) for m in RP_REC.finditer(text) if m.group(3) != " same"]


def log_size():
    return os.path.getsize(LOG)


def log_since(off):
    with open(LOG, "rb") as f:
        f.seek(off)
        return f.read().decode("ascii", "replace")


def log_before(off, span=1 << 20):
    with open(LOG, "rb") as f:
        f.seek(max(0, off - span))
        return f.read(off - max(0, off - span)).decode("ascii", "replace")


def save_rplog(capdir, rplog, off):
    """Keep the run's RP log slice, and the log ahead of it that resolves its first repeats, next to
    the capture so `decode` compares exactly like the live run. Returns that context."""
    before = log_before(off, 1 << 18)
    open(os.path.join(capdir, "rplog.txt"), "w", newline="").write(rplog)
    open(os.path.join(capdir, "rplog_before.txt"), "w", newline="").write(before)
    return before


def wait_rp_flush(start, end, buffers=2, timeout=15):
    """The RP prints what it captured one buffer at a time, seconds late. Wait for `buffers`
    summaries written after `end` (when the capture stopped), return the log from `start`."""
    deadline = time.time() + timeout
    while time.time() < deadline and log_since(end).count("Total:") < buffers:
        time.sleep(0.5)
    return log_since(start)


def rpcmp(capdir, text, before=""):
    """Every 8-byte DATA an interrupt endpoint returned to an IN, against every report the RP
    printed, repeats included (see rp_records). EP0 (descriptors) must not be forwarded since
    2026-10-10, and a report equal to the previous one on its endpoint must not be shipped since
    2026-10-11.

    The analyzer is not infallible on long captures: a report it decoded with a bad CRC (its own
    verdict, the host ACKed the real one) is unknown, not evidence, and an "Error packet" it could not
    decode at all may hide one report the RP has. Differences explained by exactly those are counted
    as analyzer errors; anything else fails. Returns (ok, wire reports, analyzer errors)."""
    pk, errors, _ = packets(capdir)
    txns = transactions(pk)
    all8 = [t for t in txns if t.token == "IN" and t.dpid and len(t.data) == 8]
    wire = [t for t in all8 if t.ep != 0]
    rp = rp_records(text, before)
    leaked = sum(1 for r in rp if r[1] == 0)
    prev, resent = {}, 0
    for a, e, d, shipped in rp:
        resent += shipped and d is not None and prev.get((a, e)) == d
        prev[(a, e)] = d
    unresolved = sum(1 for r in rp if r[2] is None)
    undecoded = sum(1 for _, v in errors if v == "Error packet")
    print("  wire 8-byte IN data: interrupt %d (EP0 %d, not forwarded) | RP reports %d, shipped %d, repeats held"
          " back %d%s%s%s" % (len(wire), len(all8) - len(wire), len(rp), sum(1 for r in rp if r[3]),
                             sum(1 for r in rp if not r[3]),
                             "  !! %d EP0 records forwarded" % leaked if leaked else "",
                             "  !! %d repeats shipped" % resent if resent else "",
                             " (%d repeats of a report older than the log, bytes unknown)" % unresolved
                             if unresolved else ""))
    wb = [t.data if t.crc is not False else None for t in wire]
    rb = [r[2] for r in rp]
    same, analyzer, outside = leaked == 0 and resent == 0, 0, 0
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(a=wb, b=rb, autojunk=False).get_opcodes():
        if op == "equal":
            continue
        # The RP log slice outlasts the recording at both ends; on a keyboard that repeats its report
        # (okhi-kbd IDLE) it holds repeats the analyzer never saw. A repeat the RP did not ship cannot
        # hide a lost keystroke: a wire report missing from the RP is still a delete and fails.
        if op == "insert" and all(not r[3] for r in rp[j1:j2]):
            outside += j2 - j1
            continue
        unknown = all(x is None for x in wb[i1:i2])
        if op == "replace" and len(set(wb[i1:i2])) == 1 and all(r[2] is None and not r[3] for r in rp[j1:j2]):
            continue
        if (op == "replace" and i2 - i1 == j2 - j1 and unknown) or (op == "delete" and unknown):
            analyzer += i2 - i1
            continue
        if op == "insert" and j2 - j1 <= undecoded - analyzer:
            analyzer += j2 - j1
            continue
        same = False
        print("  !! %s wire[%d:%d] %s | rp[%d:%d] %s" % (op, i1, i2, [x.hex() if x else "crc?" for x in wb[i1:i2]][:6],
                                                       j1, j2, [x.hex() if x else "?" for x in rb[j1:j2]][:6]))
    seen = {(w.addr, w.ep) for w in wire}
    labels = sum(1 for r in rp if (r[0], r[1]) in seen)
    print("  %s%s%s, RP addr/ep labels found on the wire %d of %d"
          % ("SAME BYTES, SAME ORDER" if same else "DIFFERENT",
             " (apart from %d analyzer errors: bad CRC %d, undecodable packets %d)"
             % (analyzer, sum(1 for x in wb if x is None), undecoded) if analyzer else "",
             " (plus %d held-back repeats outside the recording)" % outside if outside else "",
             labels, len(rp)))
    return same, len(wire), analyzer


# ------------------------------------------------------------------ keyboard, implant, SWD

def kbd_open(port):
    import serial
    s = serial.Serial(port, 9600, timeout=0.2)
    time.sleep(0.3)
    s.read(4096)
    return s


def kbd_cmd(s, line, tmo=8):
    """PING answers PONG; everything else ends in OK or ERR, possibly after '# ...' lines."""
    s.write((line + "\r\n").encode())
    buf, t0 = b"", time.time()
    while time.time() - t0 < tmo:
        buf += s.read(512)
        if re.search(rb"^(OK|ERR|PONG)\b.*\r?\n", buf, re.M):
            break
    return buf.decode("ascii", "replace").replace("\r", "").rstrip()


IP_CACHE = os.path.join(OUTROOT, "last_ip")


def implant_ip(arg=None):
    """Argument, OKHI_IP, the last "esp wifi client, address" in the RP log, else the last address
    that answered (a fresh teraterm.log starts after the boot banner and has no address in it)."""
    ip = arg or os.environ.get("OKHI_IP")
    if not ip:
        found = re.findall(r"esp wifi client, address (\d+\.\d+\.\d+\.\d+)",
                           open(LOG, "rb").read().decode("ascii", "replace"))
        ip = found[-1] if found else None
    if not ip and os.path.exists(IP_CACHE):
        ip = open(IP_CACHE).read().strip() or None
    if ip and "esp_variant=" in http(ip, "/versions"):
        os.makedirs(OUTROOT, exist_ok=True)
        open(IP_CACHE, "w").write(ip)
    return ip


def http(ip, path, tmo=4):
    try:
        return urllib.request.urlopen("http://%s%s" % (ip, path), timeout=tmo).read().decode("ascii", "replace")
    except Exception as e:
        return "ERROR %s" % e


def usb_bus(ip, want=None, timeout=0):
    """What the RP says is attached to the capture port (/stats usb_bus, None on firmware without it).
    With `want`, wait up to `timeout` s for it: a verdict needs two 256 ms windows that agree."""
    end = time.time() + timeout
    while True:
        m = re.search(r"^usb_bus=(\w+)", http(ip, "/stats"), re.M)
        bus = m.group(1) if m else None
        if want is None or bus == want or time.time() >= end:
            return bus
        time.sleep(0.25)


def stats(ip):
    return {k: int(v) for k, v in re.findall(r"^(\w+)=(-?\d+)$", http(ip, "/stats"), re.M)}


def keylog_get(ip, start):
    """One /keylog chunk: (body, total, next, gen). A start past the end returns headers only."""
    r = urllib.request.urlopen("http://%s/keylog?from=%d" % (ip, start), timeout=10)
    h = r.headers
    return r.read(), int(h["X-Keylog-Total"]), int(h["X-Keylog-Next"]), int(h["X-Keylog-Gen"])


def keylog_since(ip, start):
    out, pos = b"", start
    while True:
        body, total, nxt, gen = keylog_get(ip, pos)
        out += body
        if not body or nxt >= total:
            return out, total, gen
        pos = nxt


WEB_PAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "webusb", "index.html")
WEB_LAYOUT = os.environ.get("OKHI_WEB_LAYOUT", "spanish")


def web_decode(path):
    """The keylog decoded by the shipping page's own parser (okhi_decode.js), as plain text."""
    r = subprocess.run(["node", os.path.join(os.path.dirname(os.path.abspath(__file__)), "okhi_decode.js"),
                        WEB_PAGE, path, WEB_LAYOUT], capture_output=True, text=True, encoding="utf-8")
    if r.returncode:
        return None
    return r.stdout.replace("[SPACE]", " ").replace("[ENTER]", "\n").replace("[TAB]", "\t")


def _rep(mod, *keys, ep=1):
    return "IN: 0x5/0x%x 0x%x 0x0 %s;\r\n" % (ep, mod, " ".join("0x%x" % k for k in (list(keys) + [0] * 6)[:6]))


ROLLOVER = (1, 1, 1, 1, 1, 1)
# How real low speed keyboards differ from okhi-kbd, each as the report sequence on the wire and what
# the page must show (english layout). okhi-kbd packs held keys from slot 0; plenty of keyboards don't.
VARIANTS = [
    ("packed slots (okhi-kbd)", [_rep(0, 4), _rep(0, 4, 5), _rep(0, 5), _rep(0, 5, 6), _rep(0, 6), _rep(0)], "abc"),
    ("fixed slots, holes", [_rep(0, 4), _rep(0, 4, 5), _rep(0, 0, 5), _rep(0, 0, 5, 6), _rep(0, 0, 0, 6), _rep(0)],
     "abc"),
    ("fixed slots, slot reused", [_rep(0, 4), _rep(0, 4, 5), _rep(0, 0, 5), _rep(0, 6, 5), _rep(0, 6), _rep(0)], "abc"),
    ("keys only in the last slots", [_rep(0, 0, 0, 0, 0, 0, 4), _rep(0), _rep(0, 0, 0, 5), _rep(0)], "ab"),
    ("phantom state (ErrorRollOver) while keys held",
     [_rep(0, 4), _rep(0, 4, 5), _rep(0, *ROLLOVER), _rep(0, 4, 5, 6), _rep(0, 5, 6), _rep(0)], "abc"),
    ("idle rate: the same report repeated", [_rep(0, 4)] * 3 + [_rep(0)] * 2 + [_rep(0, 5)] * 4 + [_rep(0)], "ab"),
    ("same key twice", [_rep(0, 4), _rep(0), _rep(0, 4), _rep(0)], "aa"),
    ("two new keys in one report", [_rep(0, 4, 5), _rep(0)], "ab"),
    ("shift in the same report as the key", [_rep(2, 4), _rep(0), _rep(0x20, 5), _rep(0)], "AB"),
    ("shift held over keys", [_rep(2), _rep(2, 4), _rep(2), _rep(2, 5), _rep(0), _rep(0, 6), _rep(0)], "ABc"),
    ("shift pressed while a key is held", [_rep(0, 4), _rep(2, 4), _rep(2), _rep(2, 4), _rep(0)], "aA"),
    ("CapsLock", [_rep(0, 57), _rep(0), _rep(0, 4), _rep(2, 5), _rep(0), _rep(0, 57), _rep(0), _rep(0, 6), _rep(0)],
     "Abc"),
    ("Ctrl+C", [_rep(1), _rep(1, 6), _rep(1), _rep(0)], "[CTRL]c"),
    ("second interface mirrors an empty report",
     [_rep(0, 4), _rep(0, ep=2), _rep(0, 4, 5), _rep(0, ep=2), _rep(0, 5), _rep(0, ep=2), _rep(0)], "ab"),
    ("keyboard on endpoint 3", [_rep(0, 4, ep=3), _rep(0, ep=3), _rep(0, 5, ep=3), _rep(0, ep=3)], "ab"),
    ("media key with a report id, padded to 8 bytes",
     ["IN: 0x5/0x2 0x2 0xe9 0x0 0x0 0x0 0x0 0x0 0x0;\r\n", _rep(0, 4), "IN: 0x5/0x2 0x2 0x0 0x0 0x0 0x0 0x0 0x0 0x0;\r\n",
      _rep(0)], "a"),
    ("keypad, F keys, edit keys", [_rep(0, 89), _rep(0), _rep(0, 58), _rep(0), _rep(0, 76), _rep(0)],
     "[KP1][F1][DELETE]"),
    ("international and F13+ keys print nothing", [_rep(0, 0x87), _rep(0), _rep(0, 0x68), _rep(0), _rep(0, 4), _rep(0)],
     "a"),
    ("old log: UTF-16 string descriptor on endpoint 0",
     ["IN: 0x0/0x0 0x75 0x0 0x69 0x0 0x6e 0x0 0x6f 0x0;\r\n", _rep(0, 4, ep=0), _rep(0, ep=0)], "a"),
]


def cmd_variants():
    """The shipping page's decoder against every report variant above, no hardware."""
    os.makedirs(OUTROOT, exist_ok=True)
    path = os.path.join(OUTROOT, "variant.txt")
    bad = 0
    for name, recs, want in VARIANTS:
        open(path, "w", newline="").write("".join(recs))
        r = subprocess.run(["node", os.path.join(os.path.dirname(os.path.abspath(__file__)), "okhi_decode.js"),
                            WEB_PAGE, path, "english"], capture_output=True, text=True, encoding="utf-8")
        ok = r.returncode == 0 and r.stdout == want
        bad += not ok
        print("%-4s %-50s %r%s" % ("OK" if ok else "BAD", name, r.stdout, "" if ok else "  want %r" % want))
    print("VARIANTS %s: %d/%d" % ("PASSED" if not bad else "FAILED", len(VARIANTS) - bad, len(VARIANTS)))
    return 1 if bad else 0


def swd_probe():
    exe = os.path.join(OPENOCD, "openocd.exe")
    r = subprocess.run([exe, "-f", "scripts/interface/cmsis-dap.cfg", "-f", "scripts/target/rp2040.cfg",
                        "-c", "adapter speed 5000", "-c", "init", "-c", "targets", "-c", "shutdown"],
                       cwd=OPENOCD, capture_output=True, text=True, timeout=30)
    return (r.stdout + r.stderr).count("Examination succeed")


# ------------------------------------------------------------------ commands

def cmd_check(ip_arg):
    import serial.tools.list_ports as lp
    bad = 0
    print("ports:")
    for p in lp.comports():
        print("  %-6s %04X:%04X %-45s %s" % (p.device, p.vid or 0, p.pid or 0, p.description,
                                             PORT_ROLES.get((p.vid, p.pid), "")))
    age = time.time() - os.path.getmtime(LOG)
    tail = log_since(max(0, log_size() - 4000))
    print("RP log: %s, last write %.0f s ago%s" % (LOG, age, "" if age < 10 else "  !! NOT LIVE"))
    for key in ("external ESP-RESET", "esp link", "Total:"):
        hits = [l for l in tail.splitlines() if key in l]
        if hits:
            print("  %s" % hits[-1].strip())
    bad += age >= 10
    try:
        s = kbd_open(KBD_PORT)
        kbd_cmd(s, "SET ECHO OFF")
        pong = kbd_cmd(s, "PING")
        info = kbd_cmd(s, "INFO")
        s.close()
        print("keyboard %s: %s" % (KBD_PORT, "PONG" if "PONG" in pong else "!! no PONG"))
        for l in info.splitlines():
            if l.startswith(("# okhi", "# mode", "# usb")):
                print("  " + l)
        bad += "PONG" not in pong
    except Exception as e:
        print("keyboard %s: !! %s" % (KBD_PORT, e))
        bad += 1
    ip = implant_ip(ip_arg)
    v = http(ip, "/versions") if ip else "ERROR no ip"
    m = re.search(r"esp_variant=(\w+)", v)
    variant = m.group(1) if m else None
    try:
        out = capture(os.path.join(OUTROOT, "check_" + time.strftime("%Y%m%d_%H%M%S")), seconds=0.5)
        rows = [(float(r[0]), int(r[1]), int(r[2])) for r in list(csv.reader(open(os.path.join(out, "digital.csv"))))[1:]]
        occ = collections.Counter()
        for a, b in zip(rows, rows[1:]):
            occ[(a[1], a[2])] += b[0] - a[0]
        tot = sum(occ.values()) or 1
        idle = max(occ, key=occ.get)
        if variant == "ps2":
            print("analyzer (PS/2 bus, CH0 = DAT, CH1 = CLK): idle %s %.1f%%, %d transitions in 0.5 s"
                  % (idle, 100 * occ[idle] / tot, len(rows)))
            print("  expect (1, 1) both high; an inhibiting adapter holds CLK low, (1, 0), part of the time")
            bad += idle == (0, 0)
        else:
            pk, _, ka = packets(out)
            kind = {(0, 1): "USB low speed idle J (CH0 = D+ low, CH1 = D- high): mapping OK",
                    (1, 0): "USB FULL speed idle, or D+/D- swapped",
                    (1, 1): "both high: a PS/2 bus (CH0 = DAT, CH1 = CLK), not USB",
                    (0, 0): "both low: nothing attached / SE0"}[idle]
            print("analyzer: idle %s %.1f%% -> %s" % (idle, 100 * occ[idle] / tot, kind))
            print("  keep-alive %d in 0.5 s, IN tokens %d, CRC bad %d"
                  % (len(ka), sum(1 for p in pk if p.pid == "IN"), sum(1 for p in pk if p.crc_ok is False)))
            bad += idle != (0, 1)
    except Exception as e:
        print("analyzer: !! %s" % e)
        bad += 1
    try:
        n = swd_probe()
        print("SWD: %d of 2 RP2040 cores examined%s" % (n, "" if n == 2 else "  !!"))
        bad += n != 2
    except Exception as e:
        print("SWD: !! %s" % e)
        bad += 1
    print("implant %s:" % ip)
    for l in v.splitlines():
        if l.startswith(("esp_variant", "esp_image", "rp_identity", "link", "ERROR")):
            print("  " + l)
    bad += "link=up" not in v
    bus = usb_bus(ip) if "esp_variant=usb" in v else None
    if bus:
        print("  usb_bus=%s%s" % (bus, "" if bus == "low" else "  !! the web warns: the keyboard is not captured"))
        bad += bus != "low"
    print("CHECK %s" % ("PASSED" if not bad else "FAILED (%d)" % bad))
    return 0 if not bad else 1


TYPING = ("TYPE ", "LINE ", "KEY ", "COMBO ", "DOWN ", "UP ", "REL", "DELAY ", "RAW ")


class WebLoad(threading.Thread):
    """An open browser tab: the page's own polling (fetchBuffer every 0.8 s with up to 16 passes,
    /esp 2.4 s, /rp 3.4 s, /versions 10 s, /wifi 15 s), so every run loads the ESP the way a user
    watching it does. Every /buffer body is kept in order: that text is the live view's input."""

    PERIODS = (("/esp", 2.4), ("/rp", 3.4), ("/versions", 10.0), ("/wifi", 15.0), ("/stats", 0.5))
    TRACK = ("keylog_pending", "keylog_dropped", "keylog_gen", "keylog_size", "spi_records")

    def __init__(self, ip):
        super().__init__(daemon=True)
        self.ip, self.halt = ip, threading.Event()
        self.body, self.skipped, self.errors, self.requests = [], 0, 0, 0
        self.timeline = []
        self.cursor = self._buffer(2 ** 31 - 1)[0]

    def _buffer(self, cursor):
        r = urllib.request.urlopen("http://%s/buffer?from=%d" % (self.ip, cursor), timeout=5)
        h = r.headers
        return int(h["X-Okhi-Next"]), int(h["X-Okhi-Head"]), int(h["X-Okhi-Skipped"]), r.read()

    def run(self):
        due = {p: 0.0 for p, _ in self.PERIODS}
        next_buffer = 0.0
        while not self.halt.is_set():
            now = time.time()
            try:
                if now >= next_buffer:
                    next_buffer = now + 0.8
                    for _ in range(16):
                        nxt, head, skipped, body = self._buffer(self.cursor)
                        self.requests += 1
                        self.skipped += skipped
                        self.body.append(body)
                        self.cursor = nxt
                        if nxt >= head:
                            break
                for path, period in self.PERIODS:
                    if now >= due[path]:
                        due[path] = now + period
                        self.requests += 1
                        reply = http(self.ip, path)
                        if reply.startswith("ERROR"):
                            self.errors += 1
                        elif path == "/stats":
                            v = dict(re.findall(r"^(\w+)=(-?\d+)$", reply, re.M))
                            self.timeline.append((now, tuple(int(v.get(k, -1)) for k in self.TRACK)))
            except Exception:
                self.errors += 1
            time.sleep(0.05)

    def stop(self):
        self.halt.set()
        self.join(10)
        return b"".join(self.body)


def kbd_reports(s):
    """okhi-kbd's own count of reports the host took (USB block), the device-side oracle."""
    m = re.search(r"reports ok=(\d+) failed=(\d+)", kbd_cmd(s, "USB"))
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def kbd_config(port, kro=None, poll=None, timeout=25):
    """Switch report format and/or polling, then wait until the host has configured it again."""
    s = kbd_open(port)
    kbd_cmd(s, "SET ECHO OFF")
    for cmd in (("KRO %s" % kro) if kro else None, ("POLL %s" % poll) if poll else None):
        if cmd:
            kbd_cmd(s, cmd)
            time.sleep(3)
    want_kro = "kro=%s" % kro.lower() if kro else ""
    want_poll = "interval=%s" % ("10" if str(poll).upper() == "DEFAULT" else poll) if poll else ""
    line, end = "", time.time() + timeout
    while time.time() < end:
        line = next((l for l in kbd_cmd(s, "INFO").splitlines() if l.startswith("# usb")), "")
        if "usb=ready" in line and want_kro in line and want_poll in line:
            break
        time.sleep(1)
    s.close()
    return line


def cmd_type(text, port):
    return run_keys(["TYPE " + text], text, port)["code"]


def cmd_keys(cmds, text, port, notepad=None):
    return run_keys(cmds, text, port, notepad)["code"]


def run_keys(cmds, text, port, notepad=None):
    """Send okhi-kbd typing commands into the bench tab while capturing and while a simulated
    browser polls the implant, then check every oracle. `text` is what the web decoder must show,
    `notepad` what the host must show (None: the same, "-": not checked). Always ends with REL."""
    r = {"code": 2, "capture_ok": False, "host_ok": False}
    bad = [c for c in cmds if not c.upper().startswith(TYPING)]
    if bad:
        print("ABORT: not a typing command: %r" % bad)
        return r
    hwnd = ps.notepad_hwnd()
    if not hwnd or not ps.bench_clear(hwnd):
        print("ABORT: could not open and clear %s in Notepad, refusing to type" % ps.BENCH_FILE)
        return r
    ip = implant_ip(None)
    st0 = stats(ip)
    gen0 = keylog_get(ip, 1 << 30)[3]
    web = WebLoad(ip)
    web.start()
    s = kbd_open(port)
    kbd_cmd(s, "SET ECHO OFF")
    kbd0 = kbd_reports(s)
    off = log_size()
    res = {"replies": []}
    arrivals = []

    def watch_esp():
        end = time.time() + 15
        while time.time() < end:
            arrivals.append((time.time(), stats(ip).get("spi_records", 0)))
            time.sleep(0.2)

    def during():
        if not ps.force_focus(hwnd):
            res["abort"] = "could not focus the bench tab, nothing typed"
            return
        try:
            for c in cmds:
                if not ps.is_bench(ps.user32.GetForegroundWindow()):
                    res["abort"] = "bench tab lost focus before %r" % c
                    break
                res["replies"].append(kbd_cmd(s, c, tmo=10 + len(c)).splitlines()[-1:])
        finally:
            kbd_cmd(s, "REL")
        res["focus_kept"] = ps.is_bench(ps.user32.GetForegroundWindow())
        res["t_end"] = time.time()
        threading.Thread(target=watch_esp, daemon=True).start()
        res["kbd1"] = kbd_reports(s)
        time.sleep(0.3)

    try:
        out = capture(os.path.join(OUTROOT, "keys_" + time.strftime("%Y%m%d_%H%M%S")), during=during)
    except BaseException:
        web.stop()
        raise
    finally:
        s.close()
    if "abort" in res:
        print("ABORT: %s" % res["abort"])
    print("sent %d commands, replies %s, bench tab kept focus: %s"
          % (len(cmds), sorted({x[0] if x else "" for x in res["replies"]}), res.get("focus_kept")))
    rplog = wait_rp_flush(off, log_size())
    before = save_rplog(out, rplog, off)
    got = ps.bench_text(hwnd)
    summary(out, verbose=False)
    same, n_wire, analyzer = rpcmp(out, rplog, before)
    shipped = rp_shipped(rplog)
    expected = len(shipped)
    rp_text = rp_decode(shipped, out)
    ok = esp_oracles(ip, st0, gen0, expected, text, rp_text, shipped, out)
    time.sleep(1.7)
    live = web.stop()
    open(os.path.join(out, "live_buffer.txt"), "wb").write(live)
    live_text = web_decode(os.path.join(out, "live_buffer.txt"))
    live_n = live.count(b"IN: ")
    live_ok = live_text == rp_text and live_n == expected and web.skipped == 0
    print("  live view (simulated page, %d requests, %d errors): %d records, skipped %d, %s"
          % (web.requests, web.errors, live_n, web.skipped, "== the RP" if live_ok else "!! != the RP"))
    if web.timeline:
        t0, first = web.timeline[0]
        last = web.timeline[-1][1]
        print("  ESP keylog during the run: pending max %d, dropped +%d, rotations %d"
              % (max(s[0] for _, s in web.timeline), last[1] - first[1], last[2] - first[2]))
        prev = first
        for t, s in web.timeline:
            if s[1] != prev[1] or s[2] != prev[2]:
                print("    t=%5.1f s pending %4d dropped %5d gen %d size %7d" % (t - t0, s[0], s[1], s[2], s[3]))
            prev = s
    kbd1 = res.get("kbd1", (None, None))
    kbd = kbd1[0] - kbd0[0] if None not in (kbd0[0], kbd1[0]) else None
    print("  okhi-kbd says the host took %s reports (failed +%s) %s"
          % (kbd, kbd1[1] - kbd0[1] if None not in (kbd0[1], kbd1[1]) else None,
             "== wire" if kbd == n_wire else "(wire %d)" % n_wire))
    reached = [t for t, n in arrivals if n - st0.get("spi_records", 0) >= expected]
    latency = reached[0] - res["t_end"] if reached and "t_end" in res else None
    print("  latency: the last record reached the ESP %s after the last keystroke"
          % ("%.1f s" % latency if latency is not None else "?? (not within 15 s)"))
    want = text if notepad is None else notepad
    host_ok = want == "-" or got == want
    if want != "-":
        print("  Notepad %r %s" % (got if len(got) < 300 else got[:150] + " ... " + got[-150:],
                                   "== expected" if got == want else "!! != %r" % want))
    capture_ok = same and ok and live_ok and "abort" not in res
    web_ok = rp_text == text
    print("VERDICT capture %s, web %s, host %s" % ("OK" if capture_ok else "FAILED",
                                                 "OK" if web_ok else "shows something else for these keys",
                                                 "OK" if host_ok else "typed something else (2.3: the PC, not okhi)"))
    r.update(code=0 if capture_ok and web_ok and host_ok else 1, capture_ok=capture_ok, web_ok=web_ok,
             host_ok=host_ok, analyzer=analyzer, wire=n_wire,
             rp=expected, kbd=kbd, live=live_n, latency=latency, out=out)
    return r


def corpus(n_lines, seed, width=190):
    """Reproducible typing text: words, capitals, numbers and . , - (no AltGr, no dead keys)."""
    import random
    rnd = random.Random(seed)
    lines = []
    for _ in range(n_lines):
        s = ""
        while len(s) < width:
            r = rnd.random()
            w = "".join(rnd.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(rnd.randint(2, 9)))
            if r < 0.2:
                w = w.capitalize()
            elif r < 0.3:
                w = str(rnd.randint(0, 9999))
            elif r < 0.36:
                w += rnd.choice(".,-")
            s += w + " "
        lines.append(s[:width].rstrip())
    return lines


def run_soak(n_lines, seed, press, gap, port):
    """Long typing run against every oracle. press/gap (ms) speed the keyboard up; its previous
    timing is read from INFO and put back afterwards."""
    lines = corpus(n_lines, seed)
    s = kbd_open(port)
    kbd_cmd(s, "SET ECHO OFF")
    m = re.search(r"press=(\d+)ms gap=(\d+)ms", kbd_cmd(s, "INFO"))
    old = (int(m.group(1)), int(m.group(2))) if m else None
    if press is not None and gap is not None:
        kbd_cmd(s, "SET PRESS %d" % press)
        kbd_cmd(s, "SET GAP %d" % gap)
    s.close()
    print("soak: %d lines, %d chars, seed %d, press/gap %s (was %s)"
          % (n_lines, sum(map(len, lines)), seed, "%s/%s" % (press, gap) if press is not None else "unchanged", old))
    try:
        return run_keys(["TYPE " + l for l in lines], "".join(lines), port)
    finally:
        if press is not None and old:
            s = kbd_open(port)
            kbd_cmd(s, "SET ECHO OFF")
            kbd_cmd(s, "SET PRESS %d" % old[0])
            kbd_cmd(s, "SET GAP %d" % old[1])
            s.close()


def cmd_soak(n_lines, seed, press, gap, port):
    return run_soak(n_lines, seed, press, gap, port)["code"]


ES_SYMBOLS = ("\u00f1\u00d1\u00e7\u00c7\u00a1\u00bf\u00ba\u00aa\u00b7\u20ac@#|\u00ac[]{}<>!\"$%&/()=?'+*-_.:,;\\"
              "0123456789")
ES_ACCENTS = ("\u00e1\u00e9\u00ed\u00f3\u00fa \u00c1\u00c9\u00cd\u00d3\u00da \u00fc\u00dc\u00ef\u00eb "
              "\u00e0\u00e8\u00ec\u00f2\u00f9 \u00e2\u00ea\u00ee\u00f4\u00fb \u00e3\u00f5 ^ ` ~ \u00a8 \u00b4")
ES_DEAD = {"\u00b4": "aeiouAEIOU", "\u00a8": "uUie", "`": "aeiou", "^": "aeiou", "~": "ao"}
ES_COMPOSED = {"\u00b4": "\u00e1\u00e9\u00ed\u00f3\u00fa\u00c1\u00c9\u00cd\u00d3\u00da", "\u00a8": "\u00fc\u00dc\u00ef\u00eb",
               "`": "\u00e0\u00e8\u00ec\u00f2\u00f9", "^": "\u00e2\u00ea\u00ee\u00f4\u00fb", "~": "\u00e3\u00f5"}


def es_web(text):
    """What the web page shows for es-ES text: dead keys are not composed (same as the PS/2 page),
    so an accented letter is its mark then the letter, and a lone mark is the mark then a space."""
    out = ""
    for ch in text:
        for mark, composed in ES_COMPOSED.items():
            if ch in composed:
                out += mark + ES_DEAD[mark][composed.index(ch)]
                break
        else:
            out += ch + " " if ch in ES_DEAD else ch
    return out


KEYPAD = ["KEY KP%d" % d for d in range(1, 10)] + ["KEY KP0 2", "KEY KPDOT", "KEY KPPLUS", "KEY KPMINUS",
                                                   "KEY KPMUL", "KEY KPDIV", "KEY KPENTER"]

# (name, okhi-kbd config before, typing commands, what the web shows, what Notepad shows or "-",
#  web layout). KP0 is tapped twice because with NumLock off it is Insert. CapsLock is toggled an
# even number of times so the host ends as it started.
CONTENT = [
    ("es-ES symbols, Shift and AltGr", None, ["TYPE " + ES_SYMBOLS], ES_SYMBOLS, None, "spanish"),
    ("es-ES dead keys and accents", None, ["TYPE " + ES_ACCENTS], es_web(ES_ACCENTS), ES_ACCENTS, "spanish"),
    ("edit and lock keys", None, ["KEY TAB", "KEY ENTER", "KEY BACKSPACE", "KEY LEFT", "KEY RIGHT", "KEY HOME",
                                  "KEY END", "KEY UPARROW", "KEY DOWNARROW", "KEY PGUP", "KEY PGDN", "KEY DELETE",
                                  "KEY ESC", "KEY F2", "KEY SCROLLLOCK 2", "KEY PAUSE"],
     "\t\n[BACKSPACE][LEFT][RIGHT][HOME][END][UP][DOWN][PGUP][PGDN][DELETE][ESC][F2][SCROLL][SCROLL][PAUSE]", "\t",
     "spanish"),
    ("numeric keypad", None, KEYPAD,
     "".join("[KP%d]" % d for d in range(1, 10)) + "[KP0][KP0][KP.][KP+][KP-][KP*][KP/][KPENTER]", "-", "spanish"),
    ("CapsLock, CAPSFIX and raw usages", None,
     ["KEY CAPSLOCK", "TYPE abc \u00f1 \u00e7", "KEY CAPSLOCK", "TYPE abc", "KEY CAPSLOCK", "RAW 00 04", "RAW 00 33",
      "KEY CAPSLOCK", "RAW 00 04"],
     "abc \u00f1 \u00e7abcA\u00d1a", "abc \u00f1 \u00e7abcA\u00d1a", "spanish"),
    ("Ctrl combination", None, ["COMBO CTRL+A", "TYPE xy"], "[CTRL]axy", "xy", "spanish"),
    ("en-US map, english page", "LAYOUT US", ["TYPE ;'[]\\=-`,./:\"{}|+_~<>?"], ";'[]\\=-`,./:\"{}|+_~<>?", "-",
     "english"),
]


def cmd_content(port):
    """Every kind of normal key once, each against every oracle and against what the web page and
    the host should show. Restores LAYOUT ES and the web layout after each test."""
    global WEB_LAYOUT
    rows = []
    for name, config, cmds, web, notepad, layout in CONTENT:
        print("\n=== %s" % name)
        if config:
            s = kbd_open(port)
            kbd_cmd(s, "SET ECHO OFF")
            kbd_cmd(s, config)
            s.close()
        WEB_LAYOUT = layout
        try:
            r = run_keys(cmds, web, port, notepad)
        finally:
            WEB_LAYOUT = os.environ.get("OKHI_WEB_LAYOUT", "spanish")
            if config:
                s = kbd_open(port)
                kbd_cmd(s, "SET ECHO OFF")
                kbd_cmd(s, "LAYOUT ES")
                s.close()
        rows.append((name, r))
        if r["code"] == 2:
            break
    print("\n%-34s %-7s %-5s %-5s %5s" % ("test", "capture", "web", "host", "wire"))
    for name, r in rows:
        print("%-34s %-7s %-5s %-5s %5s" % (name, "OK" if r["capture_ok"] else "FAILED", "OK" if r.get("web_ok") else "diff",
                                            "OK" if r["host_ok"] else "diff", r.get("wire")))
    return 0 if len(rows) == len(CONTENT) and all(r["code"] == 0 for _, r in rows) else 1


MATRIX = ([(k, p, t) for k in ("BOOT", "LSMULTI", "LSCONSUMER") for p in (10, 32) for t in ((30, 30), (10, 10), (5, 5))]
          + [("BOOT", 64, (10, 10)), ("LSMULTI", 64, (10, 10)), ("BOOT", 10, (0, 0))])


def cmd_matrix(n_lines, port):
    """Every low speed report format x polling x typing speed, each a soak against every oracle.
    Leaves okhi-kbd on KRO BOOT, POLL DEFAULT and its previous timing."""
    rows = []
    try:
        for i, (kro, poll, (press, gap)) in enumerate(MATRIX):
            print("\n=== %d/%d  KRO %s  POLL %d  press/gap %d/%d" % (i + 1, len(MATRIX), kro, poll, press, gap))
            print(kbd_config(port, kro, poll))
            r = run_soak(n_lines, 100 + i, press, gap, port)
            rows.append((kro, poll, press, gap, r))
            if r["code"] == 2:
                break
    finally:
        print("\nrestored: %s" % kbd_config(port, "BOOT", "DEFAULT"))
    path = os.path.join(OUTROOT, "matrix_%s.csv" % time.strftime("%Y%m%d_%H%M%S"))
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["kro", "poll", "press", "gap", "capture", "web", "host", "wire", "rp", "live", "kbd", "analyzer",
                    "latency_s", "dir"])
        for kro, poll, press, gap, r in rows:
            w.writerow([kro, poll, press, gap, r["capture_ok"], r.get("web_ok"), r["host_ok"], r.get("wire"), r.get("rp"),
                        r.get("live"), r.get("kbd"), r.get("analyzer"),
                        "%.1f" % r["latency"] if r.get("latency") is not None else "", r.get("out")])
    print("\n%-10s %4s %9s  %-7s %-4s %-5s %6s %6s %6s %6s %8s %7s" % ("kro", "poll", "press/gap", "capture", "web",
                                                                        "host", "wire", "rp", "live", "kbd",
                                                                        "analyzer", "latency"))
    for kro, poll, press, gap, r in rows:
        print("%-10s %4d %4d/%-4d  %-7s %-4s %-5s %6s %6s %6s %6s %8s %7s"
              % (kro, poll, press, gap, "OK" if r["capture_ok"] else "FAILED", "OK" if r.get("web_ok") else "diff",
                 "OK" if r["host_ok"] else "diff", r.get("wire"), r.get("rp"), r.get("live"), r.get("kbd"),
                 r.get("analyzer"), "%.1f s" % r["latency"] if r.get("latency") is not None else "-"))
    print("results: %s" % path)
    return 0 if rows and all(r["capture_ok"] for *_, r in rows) and len(rows) == len(MATRIX) else 1


# Shift held for two seconds (a held letter would auto-repeat in Notepad), and a key let go while
# another stays down: okhi-kbd zeroes that slot and leaves a hole, then reuses it.
REPEAT_CMDS = ["TYPE Idle repeat ABC abc 123 ", "DOWN SHIFT", "DELAY 1000", "TYPE x", "DELAY 1000", "UP SHIFT",
               "DOWN SPACE", "DOWN TAB", "UP SPACE", "DOWN SPACE", "UP SPACE", "UP TAB"]
REPEAT_TEXT = "Idle repeat ABC abc 123 X \t "


def cmd_repeat(ms, port):
    """A keyboard that ignores SET_IDLE and resends its unchanged report every `ms` (okhi-kbd IDLE), in
    every low speed format: the RP must hold every repeat back and everything else must stay exact.
    Leaves okhi-kbd on IDLE HOST, KRO BOOT, POLL DEFAULT."""
    rows = []
    try:
        for kro in ("BOOT", "LSMULTI", "LSCONSUMER"):
            print("\n=== KRO %s  IDLE %d" % (kro, ms))
            print(kbd_config(port, kro, 10))
            s = kbd_open(port)
            kbd_cmd(s, "SET ECHO OFF")
            reply = kbd_cmd(s, "IDLE %d" % ms)
            s.close()
            if not reply.endswith("OK"):
                print("ABORT: okhi-kbd answered %r to IDLE, it needs the firmware with that command" % reply)
                return 2
            rows.append((kro, run_keys(REPEAT_CMDS, REPEAT_TEXT, port)))
    finally:
        s = kbd_open(port)
        kbd_cmd(s, "SET ECHO OFF")
        kbd_cmd(s, "IDLE HOST")
        s.close()
        print(kbd_config(port, "BOOT", "DEFAULT"))
    print("\n%-10s %-7s %-4s %-5s %6s %6s %6s" % ("format", "capture", "web", "host", "wire", "rp", "live"))
    for kro, r in rows:
        print("%-10s %-7s %-4s %-5s %6s %6s %6s" % (kro, "OK" if r["capture_ok"] else "FAILED",
                                                   "OK" if r.get("web_ok") else "diff", "OK" if r["host_ok"] else "diff",
                                                   r.get("wire"), r.get("rp"), r.get("live")))
    return 0 if len(rows) == 3 and all(r["code"] == 0 for _, r in rows) else 1


def cmd_fullspeed(port):
    """A full speed keyboard on the low speed implant (okhi-kbd SPEED FULL). The RP cannot decode 12
    Mbit/s and must forward nothing, keep running and keep its ESP link; the host must still get every
    key through the implant; back at low speed the capture must be exact again. The analyzer decodes
    at low speed, so its part of the full speed run is noise. Leaves okhi-kbd on SPEED DEFAULT."""
    ip = implant_ip(None)
    text = "Full speed through the implant 123"
    s = kbd_open(port)
    kbd_cmd(s, "SET ECHO OFF")
    kbd_cmd(s, "SPEED FULL")
    s.close()
    time.sleep(3)
    try:
        line = kbd_config(port)
        print(line)
        if "speed=full" not in line or "usb=ready" not in line:
            print("ABORT: okhi-kbd did not come back at full speed")
            return 2
        bus_full = usb_bus(ip, "full", 5)
        off = log_size()
        fs = run_keys(["TYPE " + text], text, port)
        rplog = log_since(off)
    finally:
        s = kbd_open(port)
        kbd_cmd(s, "SET ECHO OFF")
        kbd_cmd(s, "SPEED DEFAULT")
        s.close()
        time.sleep(3)
        print(kbd_config(port))
    v = {k: x.strip() for k, x in re.findall(r"^(\w+)=(.*)$", http(ip, "/versions"), re.M)}
    sync = rplog.count("Synchronization error")
    print("\nfull speed: RP shipped %s records (must be 0), host %s, RP sync errors %d, buffers %d, link %s"
          % (fs.get("rp"), "OK" if fs["host_ok"] else "DIFF", sync, rplog.count("Total:"), v.get("link")))
    bus_low = usb_bus(ip, "low", 5)
    ls = run_keys(["TYPE " + text], text, port)
    alert = bus_full == "full" and bus_low == "low"
    ok = fs.get("rp") == 0 and fs["host_ok"] and v.get("link") == "up" and ls["code"] == 0 and alert
    print("FULLSPEED %s: nothing forwarded %s, host typed %s, link %s, web warning %s (usb_bus %s, then %s),"
          " low speed capture after it %s"
          % ("PASSED" if ok else "FAILED", fs.get("rp") == 0, fs["host_ok"], v.get("link"),
             "shown and cleared" if alert else "WRONG", bus_full, bus_low, "exact" if ls["code"] == 0 else "NOT exact"))
    return 0 if ok else 1


def rp_decode(shipped, capdir):
    """What the web page shows for exactly the records the RP sent: the reference for everything
    after the RP (ESP ring, live view, keylog). The RP itself is checked against the wire by rpcmp."""
    path = os.path.join(capdir, "rp_records.txt")
    with open(path, "w", newline="") as f:
        f.write("".join(line + "\r\n" for line in shipped))
    return web_decode(path)


def esp_oracles(ip, st0, gen0, expected, text, ref_text, ref_records, out, timeout=20):
    """Oracles 3 and 4: what the ESP took over SPI, and what reached the flash keylog, decoded by
    the web page's own parser and compared with the same parser run on the RP's records. The keylog
    is flushed every 5 s (sooner past 256 records). spi_records can run a record or two above the RP
    count: it is read before the run starts, so a straggler from the previous run lands in it; the
    live view and the keylog are the exact checks."""
    deadline = time.time() + timeout
    while True:
        st = stats(ip)
        if (st.get("spi_records", 0) - st0.get("spi_records", 0) >= expected and st.get("keylog_pending", 1) == 0) \
                or time.time() > deadline:
            break
        time.sleep(1.0)
    # The run's records are the last ones written, so read them off the keylog's tail: no offsets to
    # follow however often it rotated. The last `expected` lines must all be clean records, the RP's,
    # in order; a garbled line among them counts like a missing record.
    total = keylog_get(ip, 1 << 30)[1]
    log, _, gen = keylog_since(ip, max(0, total - expected * 72 - 4096))
    picked, garbage = [], 0
    for raw in reversed(log.split(b"\n")):
        line = raw.strip()
        if len(picked) == expected:
            break
        if not line:
            continue
        if RP_LINE.fullmatch(line):
            picked.append(line.decode("ascii"))
        else:
            garbage += 1
    picked.reverse()
    lines = len(picked)
    path = os.path.join(out, "keylog_tail.txt")
    open(path, "w", newline="").write("".join(p + "\r\n" for p in picked))
    dec = web_decode(path)
    same_records = picked == ref_records
    d = {k: st.get(k, 0) - st0.get(k, 0) for k in ("spi_records", "spi_gaps", "spi_dups", "spi_refused", "spi_garbled",
                                                   "ring_dropped")}
    print("  ESP: spi_records +%d (RP sent %d), spi_gaps +%d, spi_dups +%d, spi_refused +%d, spi_garbled +%d, ring_dropped +%d"
          % (d["spi_records"], expected, d["spi_gaps"], d["spi_dups"], d["spi_refused"], d["spi_garbled"], d["ring_dropped"]))
    print("  keylog: last %d records %s the RP's%s, rotations %d"
          % (lines, "==" if same_records else "!! !=", "" if not garbage else ", !! %d garbled lines" % garbage,
             gen - gen0))
    print("  web decoder (%s) on the keylog: %s, %s" % (WEB_LAYOUT, "== the RP" if dec == ref_text else "!! != the RP",
                                                      "== expected" if dec == text else "!! != expected"))
    if dec != ref_text or dec != text:
        for name, t in (("keylog", dec), ("RP", ref_text), ("expect", text)):
            print("    %-6s %r" % (name, t if t is None or len(t) < 300 else t[:150] + " ... " + t[-150:]))
        if dec and ref_text and dec != ref_text:
            i = next((k for k in range(min(len(dec), len(ref_text))) if dec[k] != ref_text[k]), min(len(dec), len(ref_text)))
            print("    first difference at char %d: keylog %r | RP %r" % (i, dec[max(0, i - 20):i + 20], ref_text[max(0, i - 20):i + 20]))
    return (d["spi_records"] >= expected and d["spi_gaps"] == 0 and same_records and not garbage
            and dec == ref_text)


def clock_state(ip):
    return dict(re.findall(r"^(\w+)=(.*)$", http(ip, "/clock?from=0"), re.M))


def cmd_clock(mode, ip, gate=5000, target=12000000, ref=0, watch=12):
    """Crystal calibration modes (com_rp_clock.h): ask for one over HTTP, wait for the RP to reboot into
    it (it takes the ESP with it), print its console, and in clock output measure GPIO21 with the
    analyzer (CH1 on both benches). off brings capture back. ref=1 makes the meter drive its 12 MHz
    self-check reference on GPIO20."""
    q = "set=%s" % mode + ("&gate=%d&target=%d&ref=%d" % (gate, target, ref) if mode == "meter" else "")
    before = clock_state(ip).get("mode")
    up0 = stats(ip).get("uptime_s", 0)
    print("request: %s" % http(ip, "/clock?" + q).splitlines()[:1])
    # Any request but off-while-off reboots the RP, which reboots the ESP: wait for that, or a change
    # of parameters within the same mode would read the old mode's console.
    reboots = not (mode == "off" and before == "off")
    end = time.time() + 90
    while time.time() < end:
        st = clock_state(ip)
        if st.get("mode") == mode and (not reboots or 0 <= stats(ip).get("uptime_s", -1) < up0):
            break
        time.sleep(1)
    else:
        print("ABORT: the implant did not report clock mode %r within 90 s (last %r)" % (mode, st.get("mode")))
        return 1
    print("implant reports mode=%s after %.0f s" % (mode, 90 - (end - time.time())))
    if mode == "off":
        return 0
    nxt, stop = 0, time.time() + watch
    while time.time() < stop:
        body = http(ip, "/clock?from=%d" % nxt)
        for row in body.splitlines():
            if row.startswith("line="):
                print("  | " + row[5:])
            elif row.startswith("next="):
                nxt = int(row[5:])
        time.sleep(1)
    if mode != "out":
        return 0
    from saleae import automation
    out = os.path.join(OUTROOT, "clock_" + time.strftime("%Y%m%d_%H%M%S"))
    os.makedirs(out, exist_ok=True)
    with automation.Manager.connect(port=10430, connect_timeout_seconds=10) as m:
        dev = [d for d in m.get_devices() if not d.is_simulation][0]
        cfg = automation.LogicDeviceConfiguration(enabled_digital_channels=[0, 1], digital_sample_rate=500_000_000)
        cc = automation.CaptureConfiguration(capture_mode=automation.TimedCaptureMode(duration_seconds=0.02))
        with m.start_capture(device_id=dev.device_id, device_configuration=cfg, capture_configuration=cc) as c:
            c.wait()
            c.export_raw_data_csv(directory=out, digital_channels=[0, 1])
    rises = {0: [], 1: []}
    prev = None
    for r in csv.reader(open(os.path.join(out, "digital.csv"), newline="")):
        if not r[0][0].isdigit():
            continue
        cur = (int(r[1]), int(r[2]))
        if prev:
            for ch in (0, 1):
                if cur[ch] and not prev[ch]:
                    rises[ch].append(float(r[0]))
        prev = cur
    ok = True
    for ch, name in ((1, "GPIO21"), (0, "GPIO20")):
        e = rises[ch]
        if len(e) > 2:
            f = (len(e) - 1) / (e[-1] - e[0])
            print("  analyzer CH%d (%s): %.6f MHz over %d cycles, %+.1f ppm from 12 MHz (the analyzer's own "
                  "timebase error is in it)" % (ch, name, f / 1e6, len(e) - 1, 1e6 * (f - 12e6) / 12e6))
            ok &= ch != 1 or abs(f - 12e6) < 12e6 * 200e-6
        else:
            print("  analyzer CH%d (%s): no clock" % (ch, name))
            ok &= ch != 1
    print("CLOCK OUTPUT %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


CHANNELS = list(range(8))


def cmd_channels(port):
    """What every analyzer channel carries, digital and analog together, and whether CH0/CH1 are the
    right way round for the bus okhi-kbd is driving. On PS/2 the keyboard sends two BAT frames
    (device to host, nothing reaches the PC as a keystroke) so there is traffic to identify; a USB
    bus has its keep-alives."""
    from saleae import automation
    out = os.path.join(OUTROOT, "channels_" + time.strftime("%Y%m%d_%H%M%S"))
    os.makedirs(out, exist_ok=True)
    s = kbd_open(port)
    kbd_cmd(s, "SET ECHO OFF")
    ps2 = "mode=ps2" in kbd_cmd(s, "INFO")
    try:
        with automation.Manager.connect(port=10430, connect_timeout_seconds=10) as m:
            dev = [d for d in m.get_devices() if not d.is_simulation][0]
            cfg = automation.LogicDeviceConfiguration(
                enabled_digital_channels=CHANNELS, digital_sample_rate=6_250_000,
                enabled_analog_channels=CHANNELS, analog_sample_rate=781_250)
            mode = automation.TimedCaptureMode(duration_seconds=3.0)
            with m.start_capture(device_id=dev.device_id, device_configuration=cfg,
                                 capture_configuration=automation.CaptureConfiguration(capture_mode=mode)) as c:
                try:
                    for _ in range(2 if ps2 else 0):
                        time.sleep(0.6)
                        kbd_cmd(s, "PS2 BAT")
                    th = threading.Thread(target=c.wait, daemon=True)
                    th.start()
                    th.join(30)
                    if th.is_alive():
                        raise RuntimeError("capture did not finish within 30 s")
                except BaseException:
                    c.stop()
                    raise
                c.export_raw_data_csv(directory=out, digital_channels=CHANNELS)
                c.export_raw_data_binary(directory=out, analog_channels=CHANNELS)
                c.save_capture(filepath=os.path.join(out, "capture.sal"))
    finally:
        s.close()

    rows = list(csv.reader(open(os.path.join(out, "digital.csv"), newline="")))
    head, data = rows[0], rows[1:]
    times = [float(r[0]) for r in data]
    print("capture %s (okhi-kbd in %s mode)" % (out, "PS/2" if ps2 else "USB"))
    for ch in CHANNELS:
        col = head.index("Channel %d" % ch)
        edges = []
        for t, r in zip(times, data):
            if not edges or int(r[col]) != edges[-1][1]:
                edges.append((t, int(r[col])))
        high = sum((edges[i + 1][0] if i + 1 < len(edges) else 3.0) - t for i, (t, v) in enumerate(edges) if v)
        lows = sorted(edges[i + 1][0] - t for i, (t, v) in enumerate(edges[:-1]) if not v)
        _b, _st, volts = ps.analog_binary(os.path.join(out, "analog_%d.bin" % ch))
        v = sorted(volts)
        if v[-1] < 0.5 and len(edges) == 1:
            what = "nothing connected"
        elif len(edges) == 1:
            what = "idle %s, no activity" % ("high" if edges[0][1] else "low")
        elif lows and 25e-6 <= lows[len(lows) // 2] <= 60e-6:
            what = "PS/2 clock-like bursts"
        elif lows and lows[len(lows) // 2] < 3e-6 and high > 2.5:
            what = "idle high with ~1 us low pulses: USB low speed D- (keep-alives)"
        else:
            what = "activity"
        print("  CH%d  %3.0f%% high  %5d edges  low pulses %4d, median %7s  analog %5.2f / %5.2f / %5.2f V  %s"
              % (ch, 100 * high / 3.0, len(edges) - 1, len(lows),
                 "%.1f us" % (1e6 * lows[len(lows) // 2]) if lows else "-", v[0], v[len(v) // 2], v[-1], what))

    tr = ps.transitions(os.path.join(out, "digital.csv"))
    if ps2:
        frames = ps.decode_frames(tr)[0]
        swapped = ps.decode_frames([(t, c, d) for t, d, c in tr])[0]
        print("  PS/2 frames with CH0 = DAT, CH1 = CLK: %s" % (" ".join("%02X" % f["byte"] for f in frames) or "none"))
        ok = any(f["byte"] == 0xAA for f in frames)
        print("CHANNELS %s" % ("OK: CH0 = DAT, CH1 = CLK (the BAT frame decodes)" if ok else
                               "SWAPPED: the frames decode with CH0 = CLK, CH1 = DAT" if swapped else
                               "FAILED: no PS/2 frame on CH0/CH1"))
    else:
        share = collections.Counter()
        for (t0, d0, c0), (t1, _d, _c) in zip(tr, tr[1:]):
            share[(d0, c0)] += t1 - t0
        idle = max(share, key=share.get) if share else None
        ok = idle == (0, 1)
        print("CHANNELS %s" % ("OK: idle J is CH0 = D+ low, CH1 = D- high (low speed)" if ok else
                               "WRONG: idle (CH0, CH1) = %s, (1, 0) is swapped or a full speed device" % (idle,)))
    return 0 if ok else 1


def cmd_reenum(port):
    from saleae import automation
    s = kbd_open(port)
    kbd_cmd(s, "SET ECHO OFF")
    off = log_size()
    trig = dict(trigger_type=automation.DigitalTriggerType.PULSE_LOW, trigger_channel_index=1,
                min_pulse_width_seconds=0.2, max_pulse_width_seconds=0.5,
                after_trigger_seconds=3.0, trim_data_seconds=0.5)
    try:
        out = capture(os.path.join(OUTROOT, "reenum_" + time.strftime("%Y%m%d_%H%M%S")), trigger=trig,
                      during=lambda: kbd_cmd(s, "REENUM"), deadline=30)
    finally:
        s.close()
    rplog = wait_rp_flush(off, log_size())
    before = save_rplog(out, rplog, off)
    summary(out)
    return 0 if rpcmp(out, rplog, before)[0] else 1


def cmd_analog(secs):
    import glob
    import statistics
    from saleae import automation
    out = os.path.join(OUTROOT, "analog_" + time.strftime("%Y%m%d_%H%M%S"))
    os.makedirs(out, exist_ok=True)
    with automation.Manager.connect(port=10430, connect_timeout_seconds=10) as m:
        dev = [d for d in m.get_devices() if not d.is_simulation][0]
        cfg = automation.LogicDeviceConfiguration(enabled_digital_channels=[0, 1], digital_sample_rate=500_000_000,
                                                  enabled_analog_channels=[0, 1], analog_sample_rate=50_000_000)
        cc = automation.CaptureConfiguration(capture_mode=automation.TimedCaptureMode(duration_seconds=secs))
        with m.start_capture(device_id=dev.device_id, device_configuration=cfg, capture_configuration=cc) as c:
            c.wait()
            c.export_raw_data_binary(directory=out, analog_channels=[0, 1])
    bins = sorted(glob.glob(os.path.join(out, "analog_*.bin")))
    _, step, dp = ps.analog_binary(bins[0])
    _, _, dm = ps.analog_binary(bins[1])
    n = min(len(dp), len(dm))

    def med(xs, scale=1.0):
        return scale * statistics.median(xs) if xs else float("nan")

    for name, v in (("CH0 D+", dp), ("CH1 D-", dm)):
        hi = [x for x in v if x > 2.0]
        lo = [x for x in v if x < 0.8]
        if not hi or not lo:
            print("%s never crossed both levels in %.3f s, capture longer" % (name, secs))
            continue
        hm, lm = statistics.median(hi), statistics.median(lo)
        a, b = lm + 0.1 * (hm - lm), lm + 0.9 * (hm - lm)
        rise, fall, i = [], [], 1
        while i < n:
            if v[i - 1] < a <= v[i] or v[i - 1] > b >= v[i]:
                up = v[i] > v[i - 1]
                j = i
                while j < n and j - i < 40 and ((v[j] < b) if up else (v[j] > a)):
                    j += 1
                (rise if up else fall).append((j - i + 1) * step)
                i = j
            i += 1
        print("%s high %.3f V (%.1f%% of time), low %.3f V, rise %.0f ns, fall %.0f ns"
              % (name, hm, 100.0 * len(hi) / n, lm, med(rise, 1e9), med(fall, 1e9)))
    xs = []
    for i in range(1, n):
        d0, d1 = dp[i - 1] - dm[i - 1], dp[i] - dm[i]
        if d0 * d1 < 0 and max(dp[i], dm[i]) > 0.5:
            xs.append(dp[i - 1] + d0 / (d0 - d1) * (dp[i] - dp[i - 1]))
    if xs:
        print("crossover %.2f V (%.2f..%.2f, spec 1.3..2.0), an idle D- near 3.0 V is the 1.5k/15k divider"
              % (statistics.median(xs), min(xs), max(xs)))
    return 0


def main():
    a = sys.argv[1:]
    if not a:
        print(open(__file__).read().split("\n\n")[1])
        return 2
    if a[0] == "check":
        return cmd_check(a[1] if len(a) > 1 else None)
    if a[0] == "idle":
        out = capture(os.path.join(OUTROOT, "idle_" + time.strftime("%Y%m%d_%H%M%S")),
                      seconds=float(a[1]) if len(a) > 1 else 1.0)
        summary(out)
        return 0
    if a[0] == "type" and len(a) > 1:
        return cmd_type(a[1], a[2] if len(a) > 2 else KBD_PORT)
    if a[0] == "soak":
        n = [int(x) for x in a[1:5]]
        return cmd_soak(n[0] if n else 3, n[1] if len(n) > 1 else 1, n[2] if len(n) > 3 else None,
                        n[3] if len(n) > 3 else None, KBD_PORT)
    if a[0] == "matrix":
        return cmd_matrix(int(a[1]) if len(a) > 1 else 3, KBD_PORT)
    if a[0] == "content":
        return cmd_content(KBD_PORT)
    if a[0] == "keys" and len(a) > 2:
        esc = lambda x: x.replace("\\t", "\t").replace("\\n", "\n")
        return cmd_keys([c.strip() for c in a[1].split(";") if c.strip()], esc(a[2]), KBD_PORT,
                        esc(a[3]) if len(a) > 3 else None)
    if a[0] == "reenum":
        return cmd_reenum(a[1] if len(a) > 1 else KBD_PORT)
    if a[0] == "analog":
        return cmd_analog(float(a[1]) if len(a) > 1 else 0.03)
    if a[0] == "variants":
        return cmd_variants()
    if a[0] == "repeat":
        return cmd_repeat(int(a[1]) if len(a) > 1 else 4, KBD_PORT)
    if a[0] == "fullspeed":
        return cmd_fullspeed(KBD_PORT)
    if a[0] == "channels":
        return cmd_channels(KBD_PORT)
    if a[0] == "clock" and len(a) > 1 and a[1] in ("out", "meter", "off"):
        ip = implant_ip(os.environ.get("OKHI_IP"))
        n = [int(x) for x in a[2:5]]
        return cmd_clock(a[1], ip, *n)
    if a[0] == "decode" and len(a) > 1:
        out = load(a[1])
        summary(out)
        if os.path.exists(os.path.join(out, "rplog.txt")):
            ctx = os.path.join(out, "rplog_before.txt")
            rpcmp(out, open(os.path.join(out, "rplog.txt"), newline="").read(),
                  open(ctx, newline="").read() if os.path.exists(ctx) else "")
        return 0
    print("unknown command, run without arguments for usage")
    return 2


if __name__ == "__main__":
    sys.exit(main())
