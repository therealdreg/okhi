#!/usr/bin/env python3

# by Dreg

# Look at the PS/2 wire itself with the logic analyzer, while the okhi-kbd board types.
#
# This is the third oracle, below the sniffer and below the host: the raw waveform. It answers
# the questions no counter can, which adapter puts glitches on an idle bus, how wide the clock
# pulses really are, and whether a byte the implant missed was actually on the wire.
#
# CH0 = DAT, CH1 = CLK, 12 MS/s. Needs Logic 2 running with Preferences > Automation enabled.
#
# usage:
#   python ps2_scope.py idle  [seconds]                 just watch a quiet bus
#   python ps2_scope.py type  "<text>" [port]           capture while the board types it
#   python ps2_scope.py compare "<text>" [port] [ip]    wire vs what the implant captured
#   python ps2_scope.py hot     "<text>" [port] [ip]    same, but with the SOAK's timing:
#                                                       /keylog?clear=1 fired 350 ms before the
#                                                       first keystroke, inside the capture, which
#                                                       is the version that actually loses bytes
#   python ps2_scope.py analog ["<text>"] [port]        digital + ANALOG, real voltages
#   python ps2_scope.py session "<adapter>" [ip]        THE ADAPTER CAMPAIGN: one identical 63 s
#                                                       experiment per adapter, driven by a REAL
#                                                       PS/2 keyboard. Saves the Saleae .sal so the
#                                                       waveform reopens in the Logic 2 GUI, plus
#                                                       the RP serial slice and the implant keylog,
#                                                       into ps2captures/<adapter>_<timestamp>/
#
# SAFETY: "type" makes a real keyboard type into whatever window has focus on this PC, so it
# forces Notepad to the foreground first and refuses to type if it cannot.

import ctypes
import ctypes.wintypes as wt
import csv
import os
import re
import sys
import time

OUT = os.path.join(os.environ.get("TEMP", "."), "okhi_scope")
# 12 MS/s is plenty for PS/2 (a 15 kHz clock) and still resolves the ~1.8 us glitches some
# adapters emit. Drop it with OKHI_SCOPE_RATE if the analyzer times out on a busy USB bus.
# Every analyzer has its own list of legal rates: the cheap FX2 clone wants 12 MS/s and rejects
# 25, a Logic Pro 8 rejects 12 and goes to 500. Ask for this, and negotiate down to whatever the
# attached device actually allows rather than failing.
SAMPLE_RATE = int(os.environ.get("OKHI_SCOPE_RATE", 50_000_000))

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32


def notepad_hwnd():
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buf, 256)
        if buf.value in ("Notepad", "ApplicationFrameWindow") and user32.IsWindowVisible(hwnd):
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(256)
            h = kernel32.OpenProcess(0x1000, False, pid.value)
            if h:
                size = wt.DWORD(256)
                ctypes.windll.kernel32.QueryFullProcessImageNameW(h, 0, name, ctypes.byref(size))
                kernel32.CloseHandle(h)
                if name.value.lower().endswith("notepad.exe"):
                    found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


# Windows refuses SetForegroundWindow to a process that is not already in the foreground, so
# attach to the current foreground thread's input queue for the duration of the call.
def force_focus(hwnd):
    for _ in range(8):
        if user32.IsIconic(hwnd):
            user32.ShowWindow(hwnd, 9)
        fg = user32.GetForegroundWindow()
        if fg == hwnd:
            return True
        me = kernel32.GetCurrentThreadId()
        other = user32.GetWindowThreadProcessId(fg, None) if fg else 0
        attached = bool(other and other != me and user32.AttachThreadInput(me, other, True))
        try:
            user32.BringWindowToTop(hwnd)
            user32.SetForegroundWindow(hwnd)
            user32.SetFocus(hwnd)
        finally:
            if attached:
                user32.AttachThreadInput(me, other, False)
        time.sleep(0.12)
        if user32.GetForegroundWindow() == hwnd:
            return True
    return False


# ---------------------------------------------------------------- waveform

def transitions(csv_path):
    """[(t, dat, clk)] at every change, as exported by Logic 2."""
    out = []
    with open(csv_path, newline="") as f:
        r = csv.reader(f)
        next(r)
        for row in r:
            out.append((float(row[0]), int(row[1]), int(row[2])))
    return out


# Sampling at hundreds of MS/s resolves the ringing on every edge: bursts of transitions only
# nanoseconds wide. A PS/2 receiver never sees those (the capture PIO samples at 133 kHz, one
# sample every 7.5 us), so they must be filtered before frames are decoded, or every frame comes
# out as garbage. How many get filtered is itself a quality figure for the adapter and its wiring.
DEBOUNCE_NS = int(os.environ.get("OKHI_SCOPE_DEBOUNCE_NS", 200))


def debounce(tr, ns=None):
    """Drop level changes that are reverted within ns. Returns (clean_tr, dropped_count)."""
    if ns is None:
        ns = DEBOUNCE_NS
    if ns <= 0:
        return tr, 0
    limit = ns * 1e-9
    dropped = 0
    out = []
    for ch in (0, 1):
        seq = []
        prev = None
        for t, d, c in tr:
            v = d if ch == 0 else c
            if prev is None or v != prev:
                seq.append([t, v])
                prev = v
        # collapse any segment shorter than the limit by deleting it and its successor, which
        # merges the two neighbours that had the same level
        i = 1
        while i < len(seq) - 1:
            if seq[i + 1][0] - seq[i][0] < limit:
                del seq[i:i + 2]
                dropped += 2
                i = max(1, i - 1)
            else:
                i += 1
        out.append(seq)

    # merge the two cleaned channels back into one joint transition list
    events = sorted([(t, 0, v) for t, v in out[0]] + [(t, 1, v) for t, v in out[1]])
    clean = []
    dat = out[0][0][1] if out[0] else tr[0][1]
    clk = out[1][0][1] if out[1] else tr[0][2]
    for t, ch, v in events:
        if ch == 0:
            dat = v
        else:
            clk = v
        if clean and clean[-1][0] == t:
            clean[-1] = (t, dat, clk)
        else:
            clean.append((t, dat, clk))
    return clean, dropped


def clk_low_pulses(tr):
    """Every CLOCK low period as (start, width_us)."""
    pulses = []
    start = None
    for t, _dat, clk in tr:
        if clk == 0 and start is None:
            start = t
        elif clk == 1 and start is not None:
            pulses.append((start, (t - start) * 1e6))
            start = None
    return pulses


# What the capture PIO can actually see. device_to_host runs at ~133.6 kHz, one sample every
# 7.5 us, so a pulse narrower than that may be missed entirely while a wider one is taken for a
# real clock edge and can desynchronise the state machine. That is the band that matters.
PIO_SAMPLE_US = 7.5


def pulses_both(tr):
    """Every low and every high period of both lines, from RAW data: (line, level, t, width_us)."""
    out = []
    for ch, name in ((0, "DAT"), (1, "CLK")):
        prev_t = tr[0][0]
        prev_v = tr[0][1] if ch == 0 else tr[0][2]
        for t, d, c in tr[1:]:
            v = d if ch == 0 else c
            if v != prev_v:
                out.append((name, prev_v, prev_t, (t - prev_t) * 1e6))
                prev_t = t
                prev_v = v
    return out


def fake_start_bits(raw, frames):
    """[(t, width_us)] of every CLOCK LOW pulse short enough to be a glitch that fell BETWEEN
    frames. Factored out of glitch_report so a caller can bucket them by time, which is how the
    session mode attributes glitches to a phase."""
    spans = [(f["t"] - 0.0002, f["t"] + f["width_us"] / 1e6 + 0.0002) for f in frames]
    out = []
    for name, lvl, t, w in pulses_both(raw):
        if name != "CLK" or lvl != 0 or not (0.2 <= w < 30.0):
            continue
        if any(a <= t <= b for a, b in spans):
            continue
        out.append((t, w))
    return out


def idle_spans(tr, min_us):
    """[(start, end)] of every stretch where DAT and CLK are both high for at least min_us."""
    out = []
    for i in range(len(tr) - 1):
        t, d, c = tr[i]
        if d == 1 and c == 1 and (tr[i + 1][0] - t) * 1e6 >= min_us:
            out.append((t, tr[i + 1][0]))
    return out


def trusted_window(tr, min_idle_us=200.0):
    """The stretch of a recording whose frames can be believed.

    A capture starts and stops at an arbitrary instant, so the first and last frames may be cut in
    half: the recorder simply began in the middle of one. Those partials are an artefact of the
    file, not behaviour of the adapter, and counting them poisons every census. A half frame
    decodes as a wrong byte, or leaves edges that fit no frame, or leaves a stub of CLOCK LOW that
    reads exactly like a fake start bit. That last one is not hypothetical: the recorded
    adapter2_aliexpress capture appeared to emit one fake start bit at t=7.893 s, and it was the
    end of the recording at 7.891 s.

    A frame is trustworthy when a real idle period sits on BOTH sides of it inside the capture, so
    the window runs from the end of the first idle span to the start of the last. 200 us rather
    than the PIO's 124 us, so the bound is never borderline.
    """
    spans = idle_spans(tr, min_idle_us)
    if len(spans) < 2:
        return None
    return spans[0][1], spans[-1][0]


def glitch_report(raw, frames):
    """Split what is on the wire into ringing, real signalling, and the pulses that can fool the
    capture state machine.

    Position is what decides. The device_to_host program sits waiting for CLOCK to go LOW, and
    takes that as a start bit. So a short CLOCK LOW **between** frames is a fake start bit and is
    the thing that desynchronises a sniffer. The same pulse width inside a frame is just the
    signal's shape, and a short CLOCK HIGH right after a frame is usually the host jumping in to
    inhibit, not a fault.
    """
    ps = pulses_both(raw)
    spans = [(f["t"] - 0.0002, f["t"] + f["width_us"] / 1e6 + 0.0002) for f in frames]

    def inside(t):
        return any(a <= t <= b for a, b in spans)

    ring = [p for p in ps if p[3] < 0.2]
    short = [p for p in ps if 0.2 <= p[3] < 30.0]
    normal = [p for p in ps if p[3] >= 30.0]

    fake_start = [p for p in short if p[0] == "CLK" and p[1] == 0 and not inside(p[2])]
    in_frame = [p for p in short if inside(p[2])]
    other = [p for p in short if p not in fake_start and p not in in_frame]

    print("  pulse census (raw, both lines, %d total):" % len(ps))
    print("      under 200 ns              : %5d   ringing, the capture PIO cannot see it" % len(ring))
    print("      200 ns to 30 us, in frame : %5d   part of the signal, harmless" % len(in_frame))
    print("      200 ns to 30 us, elsewhere: %5d   %s" % (len(other), "mostly the host inhibiting right after a frame" if other else ""))
    print("      30 us and wider           : %5d   normal signalling" % len(normal))
    print("      FAKE START BITS           : %5d   %s" % (len(fake_start),
          "<-- CLOCK went LOW between frames. THIS is what desynchronises the sniffer"
          if fake_start else "none, this adapter does not fake start bits"))
    for name, lvl, t, w in sorted(fake_start, key=lambda x: -x[3])[:15]:
        print("      CLK LOW for %6.2f us at t=%.6fs   (DAT was %d)%s"
              % (w, t, level_at(raw, t, 0),
                 "  and the PIO CAN sample it" if w >= PIO_SAMPLE_US else "  (under one PIO sample)"))
    return len(fake_start)


def level_at(tr, t, ch):
    """Line level at an instant, from the transition list."""
    v = tr[0][1] if ch == 0 else tr[0][2]
    for tt, d, c in tr:
        if tt > t:
            break
        v = d if ch == 0 else c
    return v


# A PS/2 bit cell is tens of microseconds. Anything much longer between two falling edges is
# not part of a frame, and a CLOCK held low for a long time is the host inhibiting the bus.
BIT_GAP_MAX_US = 200.0
INHIBIT_US = 200.0


def decode_frames(tr):
    """Device-to-host PS/2 set 2 frames straight off the wire, with resynchronisation.

    Data is valid while CLOCK is low, so DAT is sampled on every falling edge: start bit, eight
    data bits LSB first, parity, stop. Rather than chunking edges eleven at a time, which lets
    one stray pulse misalign everything after it, this tries to read a frame at each edge and
    steps forward by one when it does not fit. That is what makes it survive the extra pulses
    real adapters put on the wire.
    """
    edges = []
    prev = tr[0][2]
    for t, dat, clk in tr:
        if prev == 1 and clk == 0:
            edges.append((t, dat))
        prev = clk

    frames = []
    i = 0
    skipped = []
    while i + 11 <= len(edges):
        w = edges[i:i + 11]
        contiguous = all((w[k + 1][0] - w[k][0]) * 1e6 <= BIT_GAP_MAX_US for k in range(10))
        bits = [x for _t, x in w[1:9]]
        value = 0
        for n, x in enumerate(bits):
            value |= (x & 1) << n
        ok = (contiguous and w[0][1] == 0 and w[10][1] == 1
              and ((sum(bits) + w[9][1]) % 2) == 1)
        if ok:
            steps = [(w[k + 1][0] - w[k][0]) * 1e6 for k in range(10)]
            frames.append({"t": w[0][0], "byte": value, "err": "",
                           "width_us": (w[10][0] - w[0][0]) * 1e6,
                           "bit_us": sorted(steps)[len(steps) // 2]})
            i += 11
        else:
            skipped.append(w[0][0])
            i += 1
    while i < len(edges):
        skipped.append(edges[i][0])
        i += 1
    return frames, skipped


def inhibits(tr):
    """CLOCK held low far longer than a bit cell: the host telling the device to shut up."""
    out = []
    start = None
    for t, _dat, clk in tr:
        if clk == 0 and start is None:
            start = t
        elif clk == 1 and start is not None:
            w = (t - start) * 1e6
            if w > INHIBIT_US:
                out.append((start, w))
            start = None
    return out


def stats(vals):
    v = sorted(vals)
    if not v:
        return None
    return (v[0], v[len(v) // 2], v[-1])


def report(tag, csv_path):
    """Everything worth knowing about one adapter, in one block, so adapters compare directly."""
    raw = transitions(csv_path)
    tr, dropped = debounce(raw)
    # Logic 2's digital CSV always opens with one initial-level row per channel, so a completely
    # dead capture reads as "2 raw, 1 after debounce" no matter how long it ran. That is the floor
    # of the instrument, not two events on the wire; say so rather than let it read as activity.
    floor = " (the 2/1 floor: the initial level of each channel, no real edge)" if len(raw) <= 2 else ""
    print("  transitions      : %d raw, %d after debounce (%d ns)%s" % (len(raw), len(tr), DEBOUNCE_NS, floor))
    if dropped:
        print("  RINGING filtered : %d transitions   <-- edges are not clean at this sample rate" % dropped)
    if len(tr) < 2:
        print("  the bus never moved (both lines idle high for the whole window)")
        return
    span = tr[-1][0] - tr[0][0]
    print("  window           : %.3f s      DAT starts %d, CLK starts %d" % (span, tr[0][1], tr[0][2]))

    frames_for_census, _sk = decode_frames(tr)
    glitch_report(raw, frames_for_census)
    pulses = clk_low_pulses(tr)
    s = stats([p[1] for p in pulses if p[1] >= 30.0])
    if s:
        print("  CLK low, real    : %d pulses, min %.2f  median %.2f  max %.2f us" % (len(pulses), s[0], s[1], s[2]))

    inh = inhibits(tr)
    print("  host inhibits    : %d" % len(inh))
    s = stats([w for _t, w in inh])
    if s:
        print("  inhibit us       : min %.0f  median %.0f  max %.0f" % s)

    frames, skipped = decode_frames(tr)
    print("  frames decoded   : %d      edges in no frame: %d" % (len(frames), len(skipped)))
    if frames:
        print("  bytes            : %s" % " ".join("%02X" % f["byte"] for f in frames))
        s = stats([f["width_us"] for f in frames])
        print("  frame width us   : min %.0f  median %.0f  max %.0f" % s)
        # Bit cell period inside a frame gives the adapter's real clock rate.
        s = stats(f["bit_us"] for f in frames if f.get("bit_us"))
        if s:
            print("  bit cell us      : min %.2f  median %.2f  max %.2f   (~%.1f kHz)"
                  % (s[0], s[1], s[2], 1000.0 / s[1]))
        gaps = [(frames[i + 1]["t"] - frames[i]["t"]) * 1e3 for i in range(len(frames) - 1)]
        s = stats(gaps)
        if s:
            print("  frame gap ms     : min %.2f  median %.2f  max %.2f" % s)


# ---------------------------------------------------------------- analog

# A digital capture only says "the line crossed the threshold". Analog says what the voltage
# actually did, which is the difference between a real logic event and a slow edge or a
# reflection that happens to cross. The Logic Pro 8 samples both at once.
#
# Valid pairings with 2 digital + 2 analog channels on a Pro 8:
#   digital 500 MS/s  + analog 50 / 12.5 / 6.25 / 3.125 MS/s
#   digital 6.25 MS/s + analog 1.5625 / 0.78125 MS/s      <- digital too slow to see ringing
ANALOG_COMBOS = [(500_000_000, 50_000_000), (500_000_000, 12_500_000),
                 (500_000_000, 6_250_000), (500_000_000, 3_125_000),
                 (6_250_000, 1_562_500), (6_250_000, 781_250)]


# NEVER export analog as CSV. It is one text row per sample per channel, so a few seconds at a
# few MS/s is gigabytes and takes a quarter of an hour. Saleae's binary export is float32 per
# sample behind a 40 byte header, which is what this reads.
#
# Samples are kept in array('f'), four bytes each, not in python tuples. At 50 MS/s a couple of
# seconds is a hundred million samples per channel: as tuples that is gigabytes of heap and the
# machine starts swapping, as an array it is a few hundred megabytes.
def analog_binary(path):
    """(begin_time, seconds_per_sample, array('f')) from one Saleae analog .bin file."""
    import struct
    import array as _array
    # Header is 48 bytes: magic[8], version i32, type i32, begin_time double, sample_rate u64,
    # downsample u64, num_samples u64. Reading the floats from offset 40 instead of 48 silently
    # turns the last header field into two phantom samples at the start of every capture.
    with open(path, "rb") as f:
        head = f.read(48)
        if head[:8] != b"<SALEAE>":
            raise ValueError("not a saleae binary export: %s" % path)
        _version, kind = struct.unpack_from("<ii", head, 8)
        if kind != 1:
            raise ValueError("%s is not an analog export (type %d)" % (path, kind))
        begin, rate, downsample, count = struct.unpack_from("<dQQQ", head, 16)
        vals = _array.array("f")
        vals.fromfile(f, count)
    step = float(downsample) / float(rate) if rate else 0.0
    return begin, step, vals


class Analog(object):
    """Two channels of analog samples, indexable without building a tuple per sample."""

    def __init__(self, begin, step, dat, clk):
        self.begin = begin
        self.step = step
        self.dat = dat
        self.clk = clk
        self.n = min(len(dat), len(clk))

    def __len__(self):
        return self.n

    def t(self, i):
        return self.begin + i * self.step

    def chan(self, idx):
        return self.dat if idx == 1 else self.clk

    def window(self, t0, t1, idx):
        """min and max voltage between two instants, or None outside the capture."""
        if self.step <= 0:
            return None
        a = max(0, int((t0 - self.begin) / self.step))
        b = min(self.n - 1, int((t1 - self.begin) / self.step) + 1)
        if b < a:
            return None
        v = self.chan(idx)[a:b + 1]
        if not len(v):
            return None
        return (min(v), max(v))


def analog_samples(directory):
    a0 = os.path.join(directory, "analog_0.bin")
    a1 = os.path.join(directory, "analog_1.bin")
    if not (os.path.exists(a0) and os.path.exists(a1)):
        return None
    b0, s0, d = analog_binary(a0)
    _b1, _s1, c = analog_binary(a1)
    return Analog(b0, s0, d, c)


# Nothing to guard against once the 48 byte header is parsed correctly, but a sample either side
# costs nothing and keeps the edges of a buffer out of the statistics.
ANALOG_GUARD_SAMPLES = 1

# TTL thresholds, which is what the receiving side actually decides on.
V_LOW_MAX = 0.8
V_HIGH_MIN = 2.0


def analog_report(an, digital_events=None):
    if an is None or len(an) <= 2 * ANALOG_GUARD_SAMPLES:
        print("  not enough analog samples")
        return
    lo_i = ANALOG_GUARD_SAMPLES
    hi_i = len(an) - ANALOG_GUARD_SAMPLES
    for idx, name in ((1, "DAT"), (2, "CLK")):
        v = an.chan(idx)
        nhigh = nlow = nmid = 0
        hmin = lmin = 1e9
        hmax = lmax = -1e9
        for i in range(lo_i, hi_i):
            x = v[i]
            if x >= V_HIGH_MIN:
                nhigh += 1
                hmin = min(hmin, x)
                hmax = max(hmax, x)
            elif x <= V_LOW_MAX:
                nlow += 1
                lmin = min(lmin, x)
                lmax = max(lmax, x)
            else:
                nmid += 1
        total = hi_i - lo_i
        line = "  %s: idle " % name
        line += ("%.2f to %.2f V" % (hmin, hmax)) if nhigh else "never high"
        line += ", low "
        line += ("%.2f to %.2f V" % (lmin, lmax)) if nlow else "never driven low in this window"
        line += "   (%.4f%% of samples in the undefined band)" % (100.0 * nmid / total)
        print(line)

    # Time spent crossing the undefined band. A slow edge is what becomes ringing at the receiver
    # and phantom transitions on a fast analyzer.
    for idx, name in ((1, "DAT"), (2, "CLK")):
        v = an.chan(idx)
        crossings = []
        inband = None
        for i in range(lo_i, hi_i):
            x = v[i]
            if V_LOW_MAX < x < V_HIGH_MIN:
                if inband is None:
                    inband = i
            elif inband is not None:
                crossings.append((i - inband) * an.step * 1e9)
                inband = None
        cs = stats(crossings)
        if cs:
            note = "   <-- resolution limited, one sample is %.0f ns" % (an.step * 1e9)
            print("  %s edge transit ns : min %.0f  median %.0f  max %.0f   (%d edges)%s"
                  % (name, cs[0], cs[1], cs[2], len(crossings),
                     note if cs[1] <= an.step * 1e9 * 1.5 else ""))
        else:
            print("  %s edge transit    : no edges in this window" % name)


def analog_at(an, t0, t1, idx):
    return an.window(t0, t1, idx) if an is not None else None


# ---------------------------------------------------------------- capture

# The device reports its legal rates in the error message when a bad one is supplied, so a
# throwaway capture attempt is enough to discover them. Pick the highest one at or below what was
# asked for, and if there is none, the lowest available.
_RATE_CACHE = {}


def negotiate_rate(manager, device, wanted):
    from saleae import automation
    import re
    if device.device_id in _RATE_CACHE:
        allowed = _RATE_CACHE[device.device_id]
    else:
        try:
            cfg = automation.LogicDeviceConfiguration(enabled_digital_channels=[0, 1],
                                                      digital_sample_rate=wanted)
            cc = automation.CaptureConfiguration(
                capture_mode=automation.TimedCaptureMode(duration_seconds=0.1))
            with manager.start_capture(device_id=device.device_id, device_configuration=cfg,
                                       capture_configuration=cc) as c:
                c.wait()
            _RATE_CACHE[device.device_id] = None
            return wanted
        except Exception as e:
            nums = [int(n) for n in re.findall(r"(\d{6,})", str(e))]
            allowed = sorted(set(nums))
            if not allowed:
                raise
            _RATE_CACHE[device.device_id] = allowed
    if allowed is None:
        return wanted
    below = [r for r in allowed if r <= wanted]
    return below[-1] if below else allowed[0]


def capture(tag, seconds, during=None, analog=False, out=None, save_sal=None, manual=False):
    """manual=True runs an open ended capture: `during` is expected to block for as long as the
    capture should last, and the recording stops when it returns. That is what lets a human decide
    the length of an adapter session from the outside instead of guessing a duration up front."""
    from saleae import automation
    if out is None:
        out = os.path.join(OUT, tag)
    os.makedirs(out, exist_ok=True)
    with automation.Manager.connect(port=10430, connect_timeout_seconds=10) as m:
        devs = [d for d in m.get_devices() if not d.is_simulation]
        if not devs:
            print("no real device, is the analyzer plugged in?")
            return None
        if analog:
            want = os.environ.get("OKHI_SCOPE_COMBO", "3")
            dig, ana = ANALOG_COMBOS[int(want)] if want.isdigit() else ANALOG_COMBOS[0]
            print("  digital %d S/s + analog %d S/s" % (dig, ana))
            cfg = automation.LogicDeviceConfiguration(
                enabled_digital_channels=[0, 1], digital_sample_rate=dig,
                enabled_analog_channels=[0, 1], analog_sample_rate=ana)
        else:
            rate = negotiate_rate(m, devs[0], SAMPLE_RATE)
            if rate != SAMPLE_RATE:
                print("  asked for %d S/s, this device allows %d S/s" % (SAMPLE_RATE, rate))
            cfg = automation.LogicDeviceConfiguration(enabled_digital_channels=[0, 1],
                                                      digital_sample_rate=rate)
        if manual:
            cc = automation.CaptureConfiguration(capture_mode=automation.ManualCaptureMode())
        else:
            cc = automation.CaptureConfiguration(
                capture_mode=automation.TimedCaptureMode(duration_seconds=seconds))
        with m.start_capture(device_id=devs[0].device_id, device_configuration=cfg,
                             capture_configuration=cc) as c:
            if during:
                time.sleep(0.3)
                during()
            if manual:
                c.stop()
            else:
                c.wait()
            c.export_raw_data_csv(directory=out, digital_channels=[0, 1])
            if analog:
                c.export_raw_data_binary(directory=out, analog_channels=[0, 1])
            # The .sal is the Saleae SAVE file, the one that reopens in the Logic 2 GUI. The CSV
            # is for this script; the .sal is for a human to scroll through afterwards, which is
            # the whole point of keeping a session around.
            if save_sal:
                try:
                    c.save_capture(filepath=save_sal)
                except Exception as e:
                    print("  WARNING: could not save the .sal: %s" % e)
    return os.path.join(out, "digital.csv")


# Opening the serial port enumerates it on the USB bus, and that burst is enough to disturb the
# analyzer's stream. Open it before the capture starts, not inside it.
def open_board(port):
    import serial
    s = serial.Serial(port, 9600, timeout=1)
    time.sleep(0.4)
    s.reset_input_buffer()
    s.write(b"SET ECHO OFF\n")
    time.sleep(0.3)
    s.reset_input_buffer()
    return s


def type_on_board(s, text):
    s.write(("TYPE " + text + "\n").encode("utf-8"))
    deadline = time.time() + 60
    while time.time() < deadline:
        line = s.readline().decode("utf-8", "replace").strip()
        if line.startswith("OK") or line.startswith("ERR"):
            print("  board replied: %s" % line)
            return

# ---------------------------------------------------------------- wire vs sniffer

# The decisive experiment. Capture the waveform and the implant's log over the same keystrokes,
# then line the two up: a byte that is on the wire but not in the log is a confirmed loss, and
# the waveform says what the bus was doing just before it, which is what tells an idle race from
# an inhibit race.

def implant_get(host, path):
    import urllib.request
    with urllib.request.urlopen("http://%s%s" % (host, path), timeout=15) as r:
        return r.read().decode("utf-8", "replace"), dict(r.headers)


def implant_keylog(host):
    body, hdr = implant_get(host, "/keylog?from=0")
    total = int(hdr.get("X-Keylog-Total", "0"))
    out = [body]
    off = int(hdr.get("X-Keylog-Next", "0"))
    while off < total:
        body, hdr = implant_get(host, "/keylog?from=%d" % off)
        if not body:
            break
        out.append(body)
        nxt = int(hdr.get("X-Keylog-Next", "0"))
        if nxt <= off:
            break
        off = nxt
    return "".join(out)


def implant_codes(text):
    import re
    return [int(m.group(1), 16) for m in re.finditer(r"D:0x([0-9A-Fa-f]{1,2})\s+t:", text)]


def compare(text, port, host, seconds, hot_ms=None):
    """Wire vs sniffer over the same keystrokes.

    hot_ms reproduces the soak's timing instead of the leisurely one. Plain compare clears the
    keylog, then spends seconds opening the serial port and arming the analyzer before a key is
    pressed, so the bus and the implant are both long quiet by the time typing starts. The soak
    clears and types 350 ms later, and that is the version that loses bytes. Passing hot_ms moves
    the clear INSIDE the capture window, hot_ms before the first keystroke, so the waveform covers
    the HTTP request as well as the bytes it may cost.
    """
    hwnd = notepad_hwnd()
    if not hwnd:
        print("ABORT: notepad is not open, refusing to type")
        return 2
    if not force_focus(hwnd):
        print("ABORT: could not focus notepad, refusing to type")
        return 2

    if hot_ms is None:
        implant_get(host, "/keylog?clear=1")
        time.sleep(0.4)
    ser = open_board(port)

    def burst():
        if hot_ms is not None:
            t0 = time.time()
            implant_get(host, "/keylog?clear=1")
            print("  keylog cleared %.0f ms into the capture, typing in %d ms"
                  % ((time.time() - t0) * 1e3, hot_ms))
            time.sleep(hot_ms / 1000.0)
        type_on_board(ser, text)

    try:
        path = capture("compare", seconds, during=burst)
    finally:
        ser.close()
    if not path:
        return 1

    tr, dropped = debounce(transitions(path))
    if dropped:
        print("  ringing filtered : %d transitions" % dropped)
    frames, skipped = decode_frames(tr)
    # A byte can only be called lost if the wire count is trustworthy, so say out loud how many
    # falling edges the decoder could not fit into a frame.
    if skipped:
        print("  UNDECODED EDGES  : %d (the wire count below is a lower bound)" % skipped)
    wire = [f["byte"] for f in frames]

    time.sleep(6.0)
    log = implant_keylog(host)
    got = implant_codes(log)

    print("")
    print("  on the wire      : %d bytes" % len(wire))
    print("  in the implant   : %d bytes" % len(got))
    print("  wire   : %s" % " ".join("%02X" % b for b in wire))
    print("  implant: %s" % " ".join("%02X" % b for b in got))

    # Walk both streams together. The implant can only ever be missing bytes, never invent them,
    # so anything it skips is a loss and its waveform neighbourhood is worth printing.
    i = j = 0
    losses = []
    while i < len(wire):
        if j < len(got) and got[j] == wire[i]:
            i += 1
            j += 1
            continue
        losses.append(i)
        i += 1
    print("")
    if not losses:
        print("  NOTHING LOST: the implant captured every byte that was on the wire")
        quiet_all = []
        for n in range(1, len(frames)):
            q = (frames[n]["t"] - (frames[n - 1]["t"] + frames[n - 1]["width_us"] / 1e6)) * 1e3
            quiet_all.append(q)
        print("DATA gaps " + " ".join("%.3f" % q for q in quiet_all))
        return 0

    print("  LOST %d byte(s):" % len(losses))
    inh = inhibits(tr)
    # Machine readable lines so many rounds can be pooled: the question is whether losses
    # concentrate in a particular kind of gap or fall uniformly across all of them.
    quiet_all = []
    for n in range(1, len(frames)):
        q = (frames[n]["t"] - (frames[n - 1]["t"] + frames[n - 1]["width_us"] / 1e6)) * 1e3
        quiet_all.append(q)
    print("DATA gaps " + " ".join("%.3f" % q for q in quiet_all))
    for idx in losses:
        f = frames[idx]
        prev_end = frames[idx - 1]["t"] + frames[idx - 1]["width_us"] / 1e6 if idx else None
        quiet_ms = (f["t"] - prev_end) * 1e3 if prev_end else None
        # was the bus inhibited in the quiet stretch right before this byte?
        near = [(t, w) for t, w in inh if prev_end is not None and prev_end - 0.001 <= t <= f["t"]]
        print("    byte %02X at t=%.6fs" % (f["byte"], f["t"]))
        if quiet_ms is not None:
            print("        quiet for %.2f ms before it" % quiet_ms)
            print("DATA loss %.3f %s" % (quiet_ms, "inhibit" if near else "idle"))
        if near:
            print("        INHIBIT in that gap: %s" % ", ".join("%.0fus" % w for _t, w in near))
        else:
            print("        no inhibit in that gap, the bus was simply idle")
    return 1


# ---------------------------------------------------------------- adapter session
#
# One identical experiment per adapter, driven by a REAL PS/2 keyboard rather than the okhi-kbd
# board. That swap matters: a real keyboard generates its own clock and answers host commands, so
# the bus carries traffic in both directions, which the emulator never produces. It also removes
# the focus problem entirely, because a human is doing the typing, not the harness.
#
# The three phases exist to make each adapter show a different failure mode:
#
#   1  fast typing            the ordinary case, dense device-to-host traffic
#   2  a long quiet stretch   the idle detector's territory: glitches on an idle bus show here
#   3  typing WHILE another   caps lock makes Windows send Set-LEDs to the keyboard, so the host
#      keyboard spams caps    inhibits the bus and transmits mid burst. This is the only phase
#                             that exercises the inhibit path, suspect 7.3
#
# Idle settle periods top and tail the capture on purpose, so the trusted window (partial frames
# at the recording's edges are an artefact, not adapter behaviour) always has clean idle to anchor
# to at both ends.

SESSION_PHASES = [
    (0.0, 4.0, "SETTLE", "hands off, letting the bus go quiet"),
    (4.0, 22.0, "PHASE 1", "TYPE FAST on the PS/2 keyboard. Normal text, keep going, do not stop"),
    (22.0, 36.0, "PHASE 2", "HANDS OFF. Let the bus sit completely idle"),
    (36.0, 58.0, "PHASE 3", "TYPE on the PS/2 keyboard AND spam CAPS LOCK on the OTHER keyboard"),
    (58.0, 63.0, "SETTLE", "hands off again, we are done in a moment"),
]


def host_requests(tr):
    """[(t, inhibit_us)] where the host inhibited and then pulled DATA low: a Request-to-Send.

    This is the caps lock traffic of phase 3 seen from outside, and it is what inhibited_signal
    watches for; every one of them disarms the capture SM on the RP.

    DATA must go low WHILE CLOCK IS STILL LOW. The protocol order is: hold CLOCK low for at least
    ~100 us, then pull DATA low (that becomes the start bit), then release CLOCK. Looking for DATA
    low AFTER the inhibit ends instead, which this function used to do, counts the start bit of the
    next device-to-host frame as a host request. On a well behaved adapter that is a rounding
    error; on the adapter from hell, which inhibits about 3550 times in a minute, it inflated 96
    real host bytes into 298.
    """
    out = []
    for t, w in inhibits(tr):
        end = t + w / 1e6
        for tt, d, c in tr:
            if tt < t:
                continue
            if tt > end:
                break
            if d == 0 and c == 0:
                out.append((t, w))
                break
    return out


def activity_blocks(frames, min_gap_s=2.0):
    """Split the capture into bursts of typing, separated by gaps of at least min_gap_s.

    The phases are DETECTED rather than assumed. A human cannot hit a stopwatch while typing, so
    pinning the analysis to nominal times would mislabel everything the moment they ran a few
    seconds long. The deliberate quiet stretch in the middle of the session is what separates the
    blocks, and it is the one instruction that is easy to follow exactly.
    """
    if not frames:
        return []
    blocks = [[frames[0]["t"], frames[0]["t"] + frames[0]["width_us"] / 1e6]]
    for f in frames[1:]:
        end = f["t"] + f["width_us"] / 1e6
        if f["t"] - blocks[-1][1] >= min_gap_s:
            blocks.append([f["t"], end])
        else:
            blocks[-1][1] = end
    return [tuple(b) for b in blocks]


def read_log_slice(path, start):
    if start is None or start < 0:
        return ""
    try:
        with open(path, "rb") as f:
            f.seek(start)
            return f.read().decode("utf-8", "replace")
    except Exception:
        return ""


def log_length(path):
    try:
        return os.path.getsize(path)
    except Exception:
        return -1


SESSION_MAX_S = 900.0


def session(name, host, teralog, outroot):
    # The campaign compares adapters on signal quality, not just on bytes, so a session runs at the
    # glitch hunting rate rather than the everyday decoding one. An idle bus costs nothing at any
    # rate (Logic 2 stores transitions), and typing at 500 MS/s produces roughly 1200 ringing
    # transitions a second, so a whole session is still only tens of thousands of edges.
    global SAMPLE_RATE
    if "OKHI_SCOPE_RATE" not in os.environ:
        SAMPLE_RATE = 500_000_000
    stamp = time.strftime("%Y%m%d_%H%M%S")
    out = os.path.join(outroot, "%s_%s" % (re.sub(r"[^A-Za-z0-9_-]", "_", name), stamp))
    os.makedirs(out, exist_ok=True)
    stopfile = os.path.join(out, "STOP")

    print("")
    print("=" * 74)
    print(" ADAPTER SESSION: %s" % name)
    print("=" * 74)
    print(" Open ended capture. Do this on the PS/2 keyboard, at your own pace:")
    for a, b, ph, what in SESSION_PHASES:
        if ph == "SETTLE":
            continue
        print("   %-7s  %s" % (ph, what))
    print("")
    print(" The quiet stretch between the two typing bursts is the ONE thing worth getting")
    print(" right: it is what separates the phases in the analysis. Two seconds is enough.")
    print("")
    print(" Recording stops when this file appears:")
    print("   %s" % stopfile)
    print(" Everything is saved to:")
    print("   %s" % out)
    print("")

    log0 = log_length(teralog)
    if log0 < 0:
        print(" WARNING: %s not readable, the RP oracle will be missing" % teralog)

    def esp_records():
        try:
            body, _ = implant_get(host, "/stats")
            m = re.search(r"spi_records=(\d+)", body)
            return int(m.group(1)) if m else None
        except Exception:
            return None

    # A DELTA, not the running total. spi_records counts from ESP boot, so printing the absolute
    # would silently compare this session against every keystroke since the implant came up.
    esp0 = esp_records()
    try:
        implant_get(host, "/keylog?clear=1")
        time.sleep(0.5)
    except Exception as e:
        print(" WARNING: could not clear the implant keylog: %s" % e)

    def wait_for_stop():
        t0 = time.time()
        print("  RECORDING. Type whenever you are ready.")
        while not os.path.exists(stopfile):
            if time.time() - t0 > SESSION_MAX_S:
                print("  safety limit of %.0f s reached, stopping on my own" % SESSION_MAX_S)
                return
            time.sleep(0.25)
        print("  stop requested after %.1f s" % (time.time() - t0))

    sal = os.path.join(out, "capture.sal")
    path = capture("session", 0, during=wait_for_stop, out=out, save_sal=sal, manual=True)
    if not path:
        return 1

    print("")
    print(" capture done, analysing")

    rp_slice = read_log_slice(teralog, log0)
    with open(os.path.join(out, "rp_serial.txt"), "w", encoding="utf-8") as f:
        f.write(rp_slice)
    esp1 = esp_records()
    esp_delta = (esp1 - esp0) if (esp0 is not None and esp1 is not None) else None

    # The implant flushes in batches, so give the keylog task time to land the tail of the burst
    # before asking for it, or the last records read as lost when they are merely late.
    time.sleep(4.0)
    try:
        log = implant_keylog(host)
    except Exception as e:
        log = ""
        print("  WARNING: keylog download failed: %s" % e)
    with open(os.path.join(out, "keylog.txt"), "w", encoding="utf-8") as f:
        f.write(log)

    report_session(out, path, rp_slice, log, esp_delta)

    # The sentinel has done its job. Sessions are kept in the repo as evidence, and a zero byte
    # control file is not evidence.
    try:
        os.remove(stopfile)
    except Exception:
        pass

    print("")
    print("  saved:")
    for f in sorted(os.listdir(out)):
        print("    %-22s %d bytes" % (f, os.path.getsize(os.path.join(out, f))))
    print("")
    print("  open capture.sal in Logic 2 to scroll the waveform by hand.")
    return 0


def report_session(out, path, rp_slice, keylog_text, esp_delta):
    """The whole verdict for one session, from files only.

    Kept separate from the capture so a session can be RE-ANALYSED without re-recording. That is
    not a nicety: the first real session of the campaign crashed here, after the waveform was
    safely on disk, and being able to rerun the analysis is what saved it.
    """
    raw = transitions(path)
    tr, dropped = debounce(raw)
    span = raw[-1][0] - raw[0][0] if len(raw) > 1 else 0.0
    win = trusted_window(tr)
    if win:
        raw = [x for x in raw if win[0] <= x[0] <= win[1]]
        tr = [x for x in tr if win[0] <= x[0] <= win[1]]
        print("  trusted window   : %.2f to %.2f s of %.2f s" % (win[0], win[1], span))
    else:
        print("  trusted window   : NONE, the bus never went properly idle. Treat this with care")

    print("  transitions      : %d raw, %d after debounce" % (len(raw), len(tr)))
    if dropped:
        print("  RINGING filtered : %d transitions" % dropped)

    frames, skipped = decode_frames(tr)
    glitch_report(raw, frames)

    inh = inhibits(tr)
    rts = host_requests(tr)
    print("  host inhibits    : %d, of which %d were followed by a host byte (caps lock)"
          % (len(inh), len(rts)))
    s = stats([w for _t, w in inh])
    if s:
        print("  inhibit us       : min %.0f  median %.0f  max %.0f" % s)

    fakes = fake_start_bits(raw, frames)
    blocks = activity_blocks(frames)
    print("")
    print("  typing bursts detected: %d" % len(blocks))
    print("    %-3s %9s %9s %8s %10s %11s %9s"
          % ("#", "from s", "to s", "frames", "inhibits", "host bytes", "glitches"))
    for i, (a, b) in enumerate(blocks, 1):
        nf = len([f for f in frames if a <= f["t"] <= b])
        ni = len([1 for t, _w in inh if a <= t <= b])
        nr = len([1 for t, _w in rts if a <= t <= b])
        ng = len([1 for t, _w in fakes if a <= t <= b])
        print("    %-3d %9.2f %9.2f %8d %10d %11d %9d" % (i, a, b, nf, ni, nr, ng))
    # Anything outside a typing burst happened on a bus nobody was driving, which is where a
    # misbehaving adapter shows its hand.
    out_g = [g for g in fakes if not any(a <= g[0] <= b for a, b in blocks)]
    out_i = [x for x in inh if not any(a <= x[0] <= b for a, b in blocks)]
    print("    %-3s %9s %9s %8s %10d %11s %9d"
          % ("--", "idle", "gaps", "-", len(out_i), "-", len(out_g)))
    if out_g:
        print("    GLITCHES ON AN IDLE BUS, this adapter misbehaves when nobody is typing:")
        for t, w in sorted(out_g, key=lambda x: -x[1])[:10]:
            print("      CLK LOW %6.2f us at t=%.6f s%s"
                  % (w, t, "   the PIO CAN sample it" if w >= PIO_SAMPLE_US else "   (under one PIO sample)"))
    if len(blocks) >= 2:
        print("")
        print("  burst 1 is the plain fast typing, the last burst is the one with caps lock.")
        print("  A caps lock burst should show host bytes; a plain one should show none.")

    # The counting points, wire first. With a real keyboard there is no ps2_sent counter to lean
    # on, so the ANALYZER is the ground truth and everything downstream is measured against it.
    #
    # BOTH DIRECTIONS, and this matters: the RP drains host-to-device bytes into the same ring, and
    # the ESP's spi_records counts every frame with a payload whichever way it was going. Comparing
    # spi_records against device-to-host frames alone understates what the ESP received and invents
    # a loss that is not there. Phase 3 exists precisely to put host traffic on the bus, so this is
    # not a corner case here, it is half the experiment.
    def counts(text):
        d = len(re.findall(r"D:0x[0-9A-Fa-f]{1,2}\s+t:", text or ""))
        h = len(re.findall(r"H:0x[0-9A-Fa-f]{1,2}\s+t:", text or ""))
        return d, h

    wire_d = len(frames)
    wire_h = len(rts)
    rp_d, rp_h = counts(rp_slice)
    log_d, log_h = counts(keylog_text)

    print("")
    print("  the chain, each stage should equal the one above it:")
    print("    %-20s %8s %8s %8s" % ("", "dev->host", "host->dev", "total"))
    print("    %-20s %8d %8s %8s" % ("on the wire", wire_d, "~%d" % wire_h,
                                     "~%d" % (wire_d + wire_h) if wire_h else "-"))
    if rp_slice:
        print("    %-20s %8d %8d %8d" % ("the RP captured", rp_d, rp_h, rp_d + rp_h))
    else:
        print("    %-20s %8s" % ("the RP captured", "no RP log"))
    print("    %-20s %8s %8s %8s" % ("the ESP received", "-", "-",
                                     esp_delta if esp_delta is not None else "?"))
    print("    %-20s %8d %8d %8d" % ("reached the log", log_d, log_h, log_d + log_h))

    # The device-to-host column is the campaign's number and it is exact: those frames are decoded
    # from the waveform. The host-to-device one is marked ~ because it is INFERRED, from inhibits
    # that carried a Request-to-Send, and that inference is only reliable on a well behaved
    # adapter. The adapter from hell inhibits 3548 times a minute with DATA left high, burying its
    # 96 real host bytes in the noise, and the estimate collapses to 2. Where the two disagree,
    # believe the RP: it logs an H record per host byte it actually captured.
    if wire_h and rp_h and abs(wire_h - rp_h) > max(3, 0.1 * rp_h):
        print("    (the ~ estimate of host bytes disagrees with the RP's %d; on an adapter that"
              % rp_h)
        print("     inhibits heavily the estimate is unreliable, trust the RP)")
    # THE VERDICT, and the only line that needs no interpretation: the analyzer decoded the
    # waveform independently, so the wire against the log is the end to end truth. Compare the
    # BYTES, not just the counts, or a lost record and an inserted one cancel out.
    wire_bytes = [f["byte"] for f in frames]
    log_bytes = [int(m.group(1), 16) for m in re.finditer(r"D:0x([0-9A-Fa-f]{1,2})\s+t:", keylog_text or "")]
    if keylog_text:
        print("")
        if wire_bytes == log_bytes:
            print("    VERDICT: the log is IDENTICAL to the wire, byte for byte, in order.")
            print("             %d device-to-host bytes, nothing lost, nothing duplicated." % len(wire_bytes))
        else:
            print("    VERDICT: the log DIFFERS from the wire. %d bytes on the wire, %d in the log."
                  % (len(wire_bytes), len(log_bytes)))
            n = min(len(wire_bytes), len(log_bytes))
            first = next((i for i in range(n) if wire_bytes[i] != log_bytes[i]), n)
            print("             first divergence at index %d" % first)

    if rp_slice:
        # NOT a loss figure. Since the RP re-sends a record the ESP did not take, and prints once
        # per delivery, its count runs AHEAD of the wire by however many re-sends happened. Reading
        # that difference as loss reports a healthy recovery as a fault.
        d = wire_d - rp_d
        print("    -> PIO capture   : %s"
              % ("lost %d" % d if d > 0 else "lost nothing (%+d vs the analyzer, i.e. re-sends)" % -d))
        if esp_delta is not None:
            d = (rp_d + rp_h) - esp_delta
            print("    -> SPI hand off  : %s"
                  % ("%d frames the ESP did not take (re-sends, or loss: see the VERDICT)" % d
                     if d > 0 else "every frame taken"))
    if esp_delta is not None:
        d = esp_delta - (log_d + log_h)
        if d > 0:
            print("    -> inside the ESP: LOST %d on the way to flash" % d)
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mode = sys.argv[1]

    if mode == "idle":
        secs = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
        print("watching a quiet bus for %.1f s" % secs)
        path = capture("idle", secs)
        if path:
            report("idle", path)
        return 0

    if mode == "type":
        text = sys.argv[2] if len(sys.argv) > 2 else "okhi"
        port = sys.argv[3] if len(sys.argv) > 3 else "COM35"
        hwnd = notepad_hwnd()
        if not hwnd:
            print("ABORT: notepad is not open, refusing to type")
            return 2
        if not force_focus(hwnd):
            print("ABORT: could not focus notepad, refusing to type")
            return 2
        print("capturing while the board types %r" % text)
        secs = float(os.environ.get("OKHI_SCOPE_SECS", 5.0))
        ser = open_board(port)
        try:
            path = capture("type", secs, during=lambda: type_on_board(ser, text))
        finally:
            ser.close()
        if path:
            report("type", path)
        return 0

    if mode == "analog":
        text = sys.argv[2] if len(sys.argv) > 2 else ""
        port = sys.argv[3] if len(sys.argv) > 3 else "COM35"
        secs = float(os.environ.get("OKHI_SCOPE_SECS", 2.0))
        ser = None
        if text:
            hwnd = notepad_hwnd()
            if not hwnd:
                print("ABORT: notepad is not open, refusing to type")
                return 2
            if not force_focus(hwnd):
                print("ABORT: could not focus notepad, refusing to type")
                return 2
            ser = open_board(port)
        try:
            print("analog + digital capture%s" % (", typing %r" % text if text else ", idle bus"))
            path = capture("analog", secs, analog=True,
                           during=(lambda: type_on_board(ser, text)) if ser else None)
        finally:
            if ser:
                ser.close()
        if not path:
            return 1
        report("analog", path)
        adir = os.path.dirname(path)
        if os.path.exists(os.path.join(adir, "analog_0.bin")):
            print("")
            an = analog_samples(adir)
            print("  analog samples   : %d" % len(an))
            analog_report(an)
            # Every pulse the digital side flagged as short gets its real voltage measured, which
            # says whether it was a genuine logic event or an edge that merely crossed a threshold.
            raw = transitions(path)
            frames_a, _ = decode_frames(debounce(raw)[0])
            spans = [(f["t"] - 0.0002, f["t"] + f["width_us"] / 1e6 + 0.0002) for f in frames_a]
            odd = [p for p in pulses_both(raw)
                   if 0.2 <= p[3] < 30.0 and not any(a <= p[2] <= b for a, b in spans)]
            if odd:
                print("  short pulses outside any frame, with their real voltage:")
                for name, lvl, t, w in sorted(odd, key=lambda x: -x[3])[:10]:
                    idx = 1 if name == "DAT" else 2
                    ex = analog_at(an, t, t + w / 1e6, idx)
                    if ex:
                        print("      %s %s %6.2f us at t=%.6fs  voltage %.2f to %.2f V%s"
                              % (name, "LOW " if lvl == 0 else "HIGH", w, t, ex[0], ex[1],
                                 "   REAL logic low" if ex[0] < 0.8 else "   never reached a valid low"))
            else:
                print("  no short pulses outside a frame to inspect")
        return 0

    if mode == "analyze":
        if len(sys.argv) < 3:
            print("usage: python ps2_scope.py analyze <session directory> [esp_delta]")
            return 2
        d = sys.argv[2]
        csv_path = os.path.join(d, "digital.csv")
        if not os.path.exists(csv_path):
            print("no digital.csv in %s" % d)
            return 2

        def slurp(n):
            p = os.path.join(d, n)
            if not os.path.exists(p):
                return ""
            with open(p, encoding="utf-8", errors="replace") as f:
                return f.read()

        esp = int(sys.argv[3]) if len(sys.argv) > 3 else None
        print("")
        print("=" * 74)
        print(" RE-ANALYSIS: %s" % os.path.basename(os.path.abspath(d)))
        print("=" * 74)
        return report_session(d, csv_path, slurp("rp_serial.txt"), slurp("keylog.txt"), esp)

    if mode == "session":
        if len(sys.argv) < 3:
            print("usage: python ps2_scope.py session \"<adapter name>\" [ip] [teraterm.log]")
            return 2
        name = sys.argv[2]
        host = sys.argv[3] if len(sys.argv) > 3 else "192.168.1.77"
        teralog = sys.argv[4] if len(sys.argv) > 4 else r"C:\Users\regue\Desktop\teraterm.log"
        root = os.environ.get("OKHI_SESSION_DIR",
                              os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                           "..", "..", "..", "ps2captures"))
        return session(name, host, teralog, os.path.abspath(root))

    if mode in ("compare", "hot"):
        text = sys.argv[2] if len(sys.argv) > 2 else "abc ABC"
        port = sys.argv[3] if len(sys.argv) > 3 else "COM35"
        host = sys.argv[4] if len(sys.argv) > 4 else "192.168.1.77"
        secs = float(os.environ.get("OKHI_SCOPE_SECS", 8.0))
        hot = int(os.environ.get("OKHI_SCOPE_HOTMS", 350)) if mode == "hot" else None
        print("comparing the wire against the implant while typing %r%s"
              % (text, ", soak timing (clear %d ms before)" % hot if hot is not None else ""))
        return compare(text, port, host, secs, hot)

    print("unknown mode %r" % mode)
    return 2


if __name__ == "__main__":
    sys.exit(main())
