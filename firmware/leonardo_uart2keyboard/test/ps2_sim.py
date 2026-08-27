#!/usr/bin/env python3

# by Dreg

# Replay the RECORDED adapter captures through the same decoders the live bench uses.
#
# Rule 6.4 of ps2adapter.md says a PIO change must be validated against stuff/ps2caps/ in
# simulation before it goes near the board, and the campaign needs a number per adapter for
# adapters that are not currently plugged in. This is the offline half of the bench: no hardware,
# no analyzer, no typing, no focus rules, and every adapter answers in seconds.
#
# WHICH FILE IN THE ARCHIVE, AND WHY IT MATTERS
#
# Each zip under stuff/ps2caps/ holds three views of the same recording:
#
#   capturestoreplay.txt   a C array dump the RP2040 recorded ITSELF
#   pulseview.sr           sigrok, 8 MHz, 1 byte per sample, probes D0 and D1
#   saleae.sal             the Saleae save file
#
# **Use the sigrok file, not the array dump.** The arrays were sampled by the very hardware whose
# capture path is under investigation, so they are already filtered by it and cannot show anything
# finer than its own sampling; the analyzer is an independent witness. 8 MHz is 125 ns per sample,
# which resolves the ~1.8 us glitches real adapters emit (14 samples) and is far finer than the
# 7.5 us the capture PIO steps at.
#
# The decoders are IMPORTED from ps2_scope.py rather than reimplemented, so an offline verdict and
# a live one can never drift apart.
#
# usage:
#   python ps2_sim.py list
#   python ps2_sim.py wire <adapter|all>        what each adapter actually put on the wire

import os
import re
import sys
import zipfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ps2_scope as scope

HERE = os.path.dirname(os.path.abspath(__file__))
CAPS = os.path.abspath(os.path.join(HERE, "..", "..", "..", "stuff", "ps2caps"))
WORK = os.path.join(os.environ.get("TEMP", "."), "okhi_ps2sim")


def adapters():
    """Every archive under stuff/ps2caps that carries a sigrok capture."""
    out = []
    if not os.path.isdir(CAPS):
        return out
    for f in sorted(os.listdir(CAPS)):
        if not f.endswith(".zip"):
            continue
        try:
            z = zipfile.ZipFile(os.path.join(CAPS, f))
        except Exception:
            continue
        if any(n.endswith("pulseview.sr") for n in z.namelist()):
            out.append(f[:-4])
    return out


def extract(name):
    """Pull just the .sr out of the archive, once, into a work directory."""
    dst = os.path.join(WORK, name + ".sr")
    if os.path.exists(dst):
        return dst
    os.makedirs(WORK, exist_ok=True)
    z = zipfile.ZipFile(os.path.join(CAPS, name + ".zip"))
    inner = [n for n in z.namelist() if n.endswith("pulseview.sr")][0]
    with z.open(inner) as src, open(dst, "wb") as f:
        while True:
            b = src.read(1 << 20)
            if not b:
                break
            f.write(b)
    return dst


def load_sr(path):
    """(samplerate, samples) from a sigrok v2 archive. One byte per sample, bit N is probe N+1."""
    z = zipfile.ZipFile(path)
    meta = z.read("metadata").decode("utf-8", "replace")
    m = re.search(r"samplerate=\s*([\d.]+)\s*(\w*)", meta)
    rate = float(m.group(1))
    unit = (m.group(2) or "").lower()
    rate *= {"khz": 1e3, "mhz": 1e6, "ghz": 1e9}.get(unit, 1.0)
    m = re.search(r"unitsize=(\d+)", meta)
    if m and int(m.group(1)) != 1:
        raise SystemExit("unitsize %s not supported" % m.group(1))

    # logic-1-1, logic-1-2, ... in NUMERIC order; a lexical sort puts -10 before -2 and silently
    # scrambles the capture into a waveform that decodes to garbage.
    names = [n for n in z.namelist() if re.match(r"^logic-1-\d+$", n)]
    names.sort(key=lambda n: int(n.rsplit("-", 1)[1]))
    parts = [np.frombuffer(z.read(n), dtype=np.uint8) for n in names]
    return rate, np.concatenate(parts) if parts else np.empty(0, np.uint8)


def to_transitions(samples, rate, dat_bit, clk_bit):
    """[(t_seconds, dat, clk)] at every change, the shape ps2_scope's decoders expect."""
    dat = (samples >> dat_bit) & 1
    clk = (samples >> clk_bit) & 1
    packed = (dat << 1) | clk
    idx = np.flatnonzero(packed[1:] != packed[:-1]) + 1
    idx = np.concatenate(([0], idx))
    t = idx.astype(np.float64) / rate
    return list(zip(t.tolist(), dat[idx].tolist(), clk[idx].tolist()))


def pick_channels(samples):
    """Which probe is CLOCK. It carries 11 pulses per frame, DATA at most a few, so the busier
    line is the clock. Guessing this wrong decodes every frame as garbage, so it is measured."""
    edges = []
    for bit in (0, 1):
        v = (samples >> bit) & 1
        edges.append(int(np.count_nonzero(v[1:] != v[:-1])))
    clk_bit = 0 if edges[0] > edges[1] else 1
    return (1 - clk_bit), clk_bit, edges


# trusted_window lives in ps2_scope so the live bench and this offline replay can never disagree
# about which part of a recording is believable. Same reason the decoders are imported.
trusted_window = scope.trusted_window


def wire_report(name):
    path = extract(name)
    rate, samples = load_sr(path)
    dat_bit, clk_bit, edges = pick_channels(samples)
    dur = len(samples) / rate

    print("=" * 78)
    print(" %s" % name)
    print("=" * 78)
    print("  capture          : %.2f s at %.0f MS/s, %d samples" % (dur, rate / 1e6, len(samples)))
    print("  channels         : D0 %d edges, D1 %d edges -> CLK is D%d, DAT is D%d"
          % (edges[0], edges[1], clk_bit, dat_bit))

    raw = to_transitions(samples, rate, dat_bit, clk_bit)
    print("  transitions      : %d raw" % len(raw))
    tr, dropped = scope.debounce(raw)
    if dropped:
        print("  RINGING filtered : %d transitions" % dropped)

    # Everything below is computed on the TRUSTED WINDOW only. Restricting the frames but leaving
    # the pulse and inhibit census on the whole file would mix a clean measurement with edges the
    # recorder cut in half, and those are the widest, oddest looking pulses in any capture.
    win = trusted_window(tr)
    if win is None:
        print("  UNTRUSTED        : fewer than two idle periods, the whole capture may be partial")
        raw_w, tr_w = raw, tr
    else:
        raw_w = [x for x in raw if win[0] <= x[0] <= win[1]]
        tr_w = [x for x in tr if win[0] <= x[0] <= win[1]]
        print("  trusted window   : %.3f to %.3f s of %.2f s   (%d of %d transitions kept)"
              % (win[0], win[1], dur, len(tr_w), len(tr)))

    frames, skipped = scope.decode_frames(tr_w)
    print("  frames decoded   : %d      edges in no frame: %d" % (len(frames), len(skipped)))

    scope.glitch_report(raw_w, frames)
    tr = tr_w

    pulses = scope.clk_low_pulses(tr)
    s = scope.stats([p[1] for p in pulses if p[1] >= 30.0])
    if s:
        print("  CLK low, real    : %d pulses, min %.2f  median %.2f  max %.2f us" % (len(pulses), s[0], s[1], s[2]))
    inh = scope.inhibits(tr)
    print("  host inhibits    : %d" % len(inh))
    s = scope.stats([w for _t, w in inh])
    if s:
        print("  inhibit us       : min %.0f  median %.0f  max %.0f" % s)

    if frames:
        widths = [f["width_us"] for f in frames]
        s = scope.stats(widths)
        print("  frame width us   : min %.0f  median %.0f  max %.0f" % s)
        cells = [f["width_us"] / 10.0 for f in frames]
        s = scope.stats(cells)
        if s:
            print("  bit cell us      : median %.2f  -> %.2f kHz" % (s[1], 1000.0 / s[1]))
        gaps = []
        for n in range(1, len(frames)):
            gaps.append((frames[n]["t"] - (frames[n - 1]["t"] + frames[n - 1]["width_us"] / 1e6)) * 1e3)
        s = scope.stats(gaps)
        if s:
            print("  frame gap ms     : min %.2f  median %.2f  max %.2f" % s)
        print("  bytes            : %s%s"
              % (" ".join("%02X" % f["byte"] for f in frames[:40]),
                 " ..." if len(frames) > 40 else ""))
    print("")
    return {"name": name, "frames": len(frames), "skipped": len(skipped), "seconds": dur}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    mode = sys.argv[1]

    if mode == "list":
        for a in adapters():
            print("  " + a)
        return 0

    if mode == "wire":
        which = sys.argv[2] if len(sys.argv) > 2 else "all"
        names = adapters() if which == "all" else [which]
        rows = []
        for n in names:
            if n not in adapters():
                print("unknown adapter %r, try: python ps2_sim.py list" % n)
                return 2
            rows.append(wire_report(n))
        if len(rows) > 1:
            print("=" * 78)
            print(" summary")
            print("=" * 78)
            print("  %-52s %8s %8s %8s" % ("adapter", "seconds", "frames", "unfitted"))
            for r in rows:
                print("  %-52s %8.2f %8d %8d" % (r["name"], r["seconds"], r["frames"], r["skipped"]))
        return 0

    print("unknown mode %r" % mode)
    return 2


if __name__ == "__main__":
    sys.exit(main())
