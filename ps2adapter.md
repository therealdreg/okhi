# PS/2 adapter log and capture test bench

What each adapter does on the wire, what the sniffer loses with it, and how to drive the bench cold.

**Only what is not in the code.** Mechanisms are explained where they live: the fix in
[com.h](firmware/com/com.h), [com_rp_ota.h](firmware/com/com_rp_ota.h) and
[firmware/ps2/rp/okhi.c](firmware/ps2/rp/okhi.c), the instrument gotchas in
[ps2_scope.py](firmware/leonardo_uart2keyboard/test/ps2_scope.py). This file holds the
measurements, the bench rules and the conclusions.

**State (2026-08-27).** The capture path never lost a byte: 14 sessions, four adapters, PIO output
equals analyzer output every time. The 0.5 to 1.2% the campaign chased was all in the RP to ESP
hand-off and is fixed. Two adapters with opposite failure modes now read identical to the wire,
byte for byte, before and after OTA.

---

# 1. The bench

```
  KEYBOARD ───┐                                        ┌──► THIS PC
  real PS/2,  │   PS/2 wire, 5 V open drain            │    keystrokes land in the
  or okhi-kbd └──►  ADAPTER UNDER TEST  ───────────────┘    focused window, rule 5.1
  on COM35                    │
                              │  same two lines tap into the implant
                              ▼
                     LEVEL SHIFTER 5 V -> 3V3
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
     SALEAE Logic Pro 8                    RP2040  ──SPI──►  ESP32-C2
     CH0 = DAT, CH1 = CLK                     │              │
     >>> WE PROBE HERE, NOT ON <<<            │              └──WiFi──► 192.168.1.77
     >>> THE 5 V PS/2 LINE     <<<            └──► COM16 ──► teraterm.log
```

> **Every waveform in this file is the 3V3 side of the level shifter, the same signal the RP2040
> sees.** That is exactly the right place to probe for "what could the sniffer have captured".
>
> **The glitches are the adapter's, not the shifter's.** The runts, the fake start bits and the
> seizures were all confirmed earlier on the original 5 V PS/2 lines; the shifter passes them
> through, it does not invent them. What the probe point does change is **absolute levels and edge
> rates**: any table here reporting volts, undershoot or transit time through the TTL band is
> describing the shifter's output, not the adapter's drive.

**Real keyboard or emulator, and it matters.** The okhi-kbd board clocks in software and never
provokes host traffic, so it makes a bad adapter look clean: adapter 2 shows zero runts under the
emulator and 161 per session under a real keyboard. Every adapter figure in section 4 is from a
**real keyboard**.

| Thing | Where | Notes |
|---|---|---|
| okhi-kbd board | **COM35**, 9600 8N1 | `TYPE`, `KEY`, `INFO`, `PS2`. `PING` answers `PONG`, not `OK`. Send `SET ECHO OFF` first |
| okhi-kbd PS/2 out | D7 = CLK, A7 = DATA, GND | |
| implant | **192.168.1.77** | DHCP, confirm it, never assume |
| implant RP serial | **COM16** -> `C:\Users\regue\Desktop\teraterm.log` | Prints every record it drains: an oracle, see 2.2 |
| implant ESP flashing | **COM20**, CP210x | **460800 only**, 921600 fails the stub upload |
| implant SWD | CMSIS-DAP `2e8a:000c` | Recovery path, no OTA needed |
| analyzer | Saleae Logic Pro 8 `21A9:1005` | Logic 2 with **Preferences > Automation** on (gRPC 10430) |
| USBasp | okhi-kbd ICSP header | Only way to reflash the keyboard, its fuses removed the bootloader |

Cold start, all four have caught a stale assumption at some point:

```bash
curl -s http://192.168.1.77/versions            # both chips answer and agree
tail -5 /c/Users/regue/Desktop/teraterm.log     # the RP is polling, SPI link up
python firmware/leonardo_uart2keyboard/test/ps2_scope.py idle 2    # analyzer visible
[System.IO.Ports.SerialPort]::getportnames()    # ports move when things are replugged
```

---

# 2. Running a measurement

Tools live in `firmware/leonardo_uart2keyboard/test/`. Usage is in each file's header.

| Tool | For |
|---|---|
| `ps2_scope.py session` | **the campaign protocol**, below |
| `ps2_scope.py analyze <dir>` | re-run the verdict on a saved session, no hardware |
| `ps2_scope.py idle / type / compare / hot / analog` | wire probes, `analog` gives real voltages |
| `ps2_sim.py wire <adapter>` | replay `stuff/ps2caps/` offline. Use the `.sr`, never `capturestoreplay.txt`: that dump was sampled by the RP2040 itself, so it is filtered by the hardware under test |
| `test_ps2_soak.ps1` | long unattended run, emulator-driven, controlled corpus |
| `test_ps2_bisect.ps1` | isolate which harness step provokes a loss |
| `test_okhi_e2e.ps1` | fixed cases, and it names which side is at fault (2.3) |
| `okhi_decode.js` | decode a keylog with the shipping web decoder, lifted from `webps2/index.html` at run time so it cannot drift |

## 2.1 The campaign protocol

`ps2_scope.py session "<adapter>"`, real PS/2 keyboard, 500 MS/s, open ended:

1. type fast and normally
2. **stop for at least two seconds**
3. type while caps lock is spammed from a **second** keyboard

Phases are **detected from the waveform**, not timed, so only the pause needs care. Phase 3 is the
only way to get host-to-device traffic on the wire; the emulator cannot produce it. Everything
lands in `ps2captures/<adapter>_<timestamp>/` including the Saleae `.sal` for the GUI. See
[ps2captures/README.md](ps2captures/README.md).

## 2.2 The four oracles

A burst is counted at four points; the first one short owns the lost byte.

| # | Oracle | Proves |
|---|---|---|
| 1 | frames decoded from the analyzer | what reached the RP2040's pins |
| 2 | `D:0x..` in `teraterm.log` | the PIO captured it and the RP drained it |
| 3 | `spi_records` in `/stats` | the frame arrived over SPI |
| 4 | `D:0x..` in `/keylog` | it survived to flash |

**1 vs 2 isolates the PIO, 2 vs 3 the SPI hand-off, 3 vs 4 the ESP's own path.** Notepad is a fifth
check outside the chain: if Windows produced the right character, the right scancodes arrived.

- **Oracle 2 counts SENDS, not distinct records.** Since the fix the RP re-sends and prints once per
  delivery, so it runs ahead of the wire by however many re-sends happened. **The wire against the
  log is the verdict**; everything else is attribution.
- **The RP serial log perturbs what it measures.** That print is a blocking USB CDC write on core1's
  hot path, so a board with a terminal attached is not timing-identical to one without.
- **Check the log is live.** A frozen log reads exactly like "the RP captured nothing".
- **`GET /rp` is a cached print, not a live counter**, frozen for seconds at a time. Never use it
  per burst. "Poll until two reads agree" is exactly backwards: a frozen counter reads identical
  twice immediately.

## 2.3 Who is at fault

| Notepad | okhi | verdict |
|---|---|---|
| right | right | nobody |
| right | wrong | the bytes were on the bus, **the sniffer lost or misread them** |
| wrong | wrong | **the keyboard** never put them on the bus |
| wrong | right | the wire was fine, **this PC** used the wrong layout |

Notepad is not an oracle for editing keys: Enter, Tab and Backspace change the control's contents
rather than appending. And every make must have its break; an unpaired make is missing bytes even
when the text comes out right.

The okhi decoder shows Space as `[SPACE]`, Enter `[ENTER]`, Tab `[TAB]`, Backspace `[BKSP]`.
Modifiers fold into the character. **Dead keys are not composed**: `á` shows as `´a`. Current
behaviour, not a bug.

---

# 3. The analyzer

`OKHI_SCOPE_RATE` default 50 MS/s, `session` raises it to 500. The rate negotiates itself against
whatever analyzer is attached.

| Analyzer | USB id | Usable |
|---|---|---|
| Saleae Logic Pro 8 | `21A9:1005` | 25 to 500 MS/s, rejects 12 |
| FX2 clone | `0925:3881` | 12 MS/s only, and **dies with `ReadTimeout` if a USB serial adapter is active** |

**The Pro 8 needs no rate reduction.** Verified with the whole bench on USB at once: 500 MS/s
digital, and 500 MS/s digital plus 3.125 MS/s analog together, both fine. The `ReadTimeout`
warning is the clone's problem. Use 500 MS/s for glitch hunting, 50 for decoding.

## 3.1 Three ways the glitch census will mislead you

All three cost real time to learn, and none is visible from the tool's output.

**Position decides, not width.** The same 2 us pulse is meaningless inside a frame and poisonous
between them. The census excludes anything within 200 us of a frame, which is why adapter 2's 39
trailing runts correctly report as **0 fake start bits** (4.5).

**A wide enough pulse escapes the census entirely.** The threshold is 30 us. The adapter from
hell's dangerous pulse is **8.43 ms**, 280x above it, classified as normal signalling. It reports
0 fake start bits and is the worst adapter here. **Never read "0 fake start bits" as "well
behaved".**

**Narrow does not mean invisible.** A PIO `wait` samples every SM cycle, so a 1.25 us pulse against
a 7.5 us sample period is caught or missed by phase, roughly one time in six.

## 3.2 Two artefacts of the instrument, not the bus

**The 2/1 floor.** A dead-quiet capture prints `2 raw, 1 after debounce`, not zero, because Logic 2's
CSV opens with one initial-level row per channel. It says that whether it ran 3 s or 10 s.

**Partial frames at the edges.** A capture starts and stops mid-frame, and a half frame leaves a
stub of CLOCK LOW that reads exactly like a fake start bit. The recorded `adapter2_aliexpress`
capture appeared to emit one at t=7.893 s; the recording ended at 7.891 s. `trusted_window()` cuts
the capture to the span with real idle on both sides.

## 3.3 Analog

Gives real voltages, so it separates a logic event from an edge that grazed the threshold. **Read
the level shifter caveat in section 1 first**: these are its output levels, not the adapter's.

> **NEVER export analog as CSV.** One row per sample per channel: 6 s at 50 MS/s on two channels
> was heading for **12 GB**. The tool uses Saleae's binary export for analog and CSV only for
> digital.

---

# 4. The adapters

## 4.1 The one result

Four adapters, waveforms differing **by 45x in edge quality, 13x in how often they seize the bus
and 112x in how long they hold it**:

> **Adapter behaviour does not predict what the sniffer loses.** All four lost the same fraction in
> the same place, and none of it was on the wire. Rates of 0.58%, 1.24%, 0.47% and 0.75% against a
> common 0.75%: chi-square 2.6 on 3 df, p about 0.46. Nothing to explain.

Those four are **5/859, 7/566, 2/430 and 4/535 records sent**. The denominators count host traffic
too, so they run above 4.3's device-to-host frame counts by roughly one record per inhibit.

Two hypotheses died proving it. Loss tracking the inhibits (adapter 5 killed it: 4 losses from 68
inhibits). And **the inhibit path, exonerated outright**: the adapter from hell fires
`stop_device_to_host_sm()` about 3550 times a minute and it cost nothing.

Never measured live: `asus_970_pro`, **zero** inhibits in its recorded capture, the opposite
extreme.

## 4.2 Signature per adapter

Sessions `adapter1_from_hell_amazon_STABLE_20260827_062005`, `adapter2_aliexpress_20260827_050203`,
`adapter4_aliexpress_20260827_050803`, `adapter5_aliexpress_20260827_051233`. Reproduce with
`ps2_scope.py analyze`.

| | 1 from hell | 2 Aliexpress | 4 Aliexpress | 5 Aliexpress |
|---|---|---|---|---|
| capture span | 96.3 s | 20.4 s | 13.0 s | 15.1 s |
| frames decoded | 2372 | 485 | 357 | 466 |
| bit cell | 83.47 us | 83.43 us | 83.38 us | 83.38 us |
| host inhibits | **5690** | 324 | 72 | 68 |
| inhibit duration | **8418 to 25293** us | 298 to 8085 | 1018 to 1024 | 1017 to 1025 |
| inhibits carrying a host RTS | **2 of 5690** | 80 of 324 | **72 of 72** | **68 of 68** |
| **time with the bus held low** | **56.1%** | 5.5% | 0.6% | 0.5% |
| CLK HIGH runts, 0.2 to 35 us | **1359** (14.1/s) | 216 (10.6/s) | 1 | 1 |
| CLK LOW runts | 0 | **39** | 0 | 0 |
| sub-200 ns blips (see 1, shifter) | 136 | 174 | **12** | **534** |

**The bit cell is identical on all four**, 83.4 us / 11.99 kHz. That is the keyboard's clock: the
adapter arbitrates the bus, it does not generate the clock, so **clock rate is never an adapter
property**. Any 9.9 kHz figure elsewhere came from the emulator.

**Two families, not a spectrum.** Adapters 4 and 5 emit essentially nothing and inhibit only to
talk. The from-hell and adapter 2 truncate clocks a dozen times a second.

> **The RTS row undercounts on the from-hell, and `analyze` says so on the way past.** It needs
> DATA low inside an inhibit, and when the adapter is holding the bus 56% of the time the real
> host bytes hide inside its seizures. The RP logged **96 host bytes in the same session**, 48
> `ED` set-LED commands with their argument, so 48 caps lock toggles. **Trust the RP's count, not
> the detector's**, on any adapter that inhibits heavily. The conclusion survives either way: 48
> of 5690 seizures is still under 1%.

## 4.3 Every session, checked against the analyzer

Fourteen sessions, four adapters, every build of the campaign. Device-to-host counts. The five
sessions in `ps2captures/` not listed here are the post-fix and post-OTA re-checks of 6.2.

| Session | wire | RP | log |
|---|---|---|---|
| from hell, original | 763 | 763 | 758 |
| from hell, ack 10 ms | 641 | 641 | 642 |
| from hell, ack 200 ms | 1673 | 1673 | 1665 |
| from hell, sequence | 2556 | 2556 | 2548 |
| from hell, dual send | 2637 | 2637 | 2634 |
| from hell, stable | 2372 | 2372 | 2367 |
| from hell, reconciliation | 2750 | 70096 | **2750** |
| 2 Aliexpress, first | 716 | 717 | 710 |
| 2 Aliexpress, second | 485 | 486 | 481 |
| 2 Aliexpress, instrumented | 366 | 366 | 364 |
| 2 Aliexpress, ack 10 ms | 463 | 463 | 461 |
| 2 Aliexpress, fixed | 370 | 370 | **370** |
| 4 Aliexpress | 357 | 358 | 357 |
| 5 Aliexpress | 466 | 467 | 464 |

> **The RP never once captured fewer records than the analyzer decoded.** One more means the
> trusted window trimmed an edge frame (3.2), never an invented record.

`ack 10 ms` holds 642 against 641: the only duplicate the campaign ever produced.
`reconciliation` shows 70096 sent against 2750 on the wire, the runaway re-sending, and its log
still reads exactly 2750: zero loss at 25x the traffic.

## 4.4 1 adapter from hell, Amazon

**Read this before touching the capture PIO.** None of it is a fault; it is what the PIO's
inhibit handling was built against.

### A 60 Hz square wave on CLOCK, for ever

| | |
|---|---|
| CLOCK held low per cycle | **8418 to 8490 us**, median 8424 |
| bus released per cycle | ~8236 us |
| **start to start** | **16667 us, exactly 60 Hz** |
| **share of all time held low** | **56.1%** |
| inhibits that were the host talking | **at most 48 of 5690** (see 4.2) |

It seizes the bus 60 times a second, **whether or not anybody is typing**, and under 1% of it is
the host wanting to transmit. Wider ones are cycles run together: 16.9 ms is 2 x 8.43, 25.3 ms is
3 x 8.43.

**Every one of those seizures is a falling edge, so every one looks like a start bit.** Left alone
`device_to_host` would step past its start-bit wait, sit for 8.43 ms, then sample the *next*
frame's clock one bit out, permanently desynchronised within a second. `inhibited_signal` raising
IRQ0 after ~90 us is what saves it, **running 60 times a second, permanently**. Weakening the
inhibit path will look fine on a good adapter and destroy capture here.

### The keyboard gets a 60 us margin

| | |
|---|---|
| frame width | 835 us |
| frames per release window | **1 or 2, never more** |
| **margin from frame end to next seizure** | **median 80 us, p25 62, min 60** |
| frames with under 200 us of margin | **1578 of 2372** |
| frames clipped inside a seizure | **0 of 2372** |

Throughput is capped by the adapter at **~120 bytes a second**. Fast typing does not make faster
bytes here, it makes a queue inside the keyboard. And the idle detector barely runs between frames:
it needs 124 us of both lines high and the median quiet stretch is 80 us.

### It truncates the last clock of every frame

1359 CLK HIGH runts, median **16.6 us** where a bit cell is 83, **every one wider than the PIO's
7.5 us sample period**. CLOCK LOW is immaculate.

```
  -44.8 us   DAT=1 CLK=0
   +0.0 us   DAT=1 CLK=1     <-- the runt: 16 us high instead of 43
  +16.4 us   DAT=1 CLK=0
+11493.6 us  DAT=1 CLK=1     the bus comes back 11.5 ms later
```

> **This is what makes the position of `push noblock` load-bearing.** It is instruction 11, BEFORE
> the trailing parity and stop waits. The byte is committed before the adapter truncates the tail.
> Move the push after those waits, which reads tidier, and this adapter loses the last byte of a
> frame **1359 times a session**.

**And the capture is still perfect**: 763 and 763, 2637 and 2637, 2372 and 2372.

## 4.5 2 Aliexpress

**A 1.25 us CLOCK LOW runt on the tail of every twelfth frame.** 39 in a 20 s session, 161 in
59.6 s, and 196 of those 200 measure between 1.24 and 1.26 us (one outlier at 2.45). An earlier
draft called these fake start bits and was wrong:

| | |
|---|---|
| distance after the previous frame ended | **56.4 to 63.3 us** |
| how many land more than 200 us after a frame | **0 of 200** |

**Not one is on an idle bus.** At 58 us past the last edge the capture SM is in its trailing parity
and stop waits, not back at the start-bit wait, so the pulse satisfies a wait instead of being read
as a start. It is the **position** that makes them harmless, not the width.

**The emulator made this adapter look clean**, which is the sharpest argument for the real-keyboard
protocol. Under the emulator: 0 runts, 0 fake start bits, and a 9.9 kHz "clock" that was the
emulator's own. Under a real keyboard: 161 runts and 216 truncated clock highs.

Electrical reference, **measured on the 3V3 side (section 1), so the level shifter is inside these
numbers**. From one live analog capture; no analog data was saved under `ps2captures/`, so unlike
every other table here this one cannot be re-derived with `analyze`:

| | DAT | CLK |
|---|---|---|
| idle / driven low | 3.3 V / ~0 V, undershoot **-0.31 V** | 3.3 V / ~0 V, undershoot **-0.33 V** |
| samples in the undefined band | 0.0000% | 0.0001% |
| edge transit 0.8 to 2.0 V | **20 to 40 ns** | **20 to 40 ns** |

Caps lock state survives host reboots; clear it before measuring (rule 5.2).

## 4.6 4 and 5 Aliexpress

On the wire, within measurement error, **the same device**.

| | 4 | 5 |
|---|---|---|
| inhibits | 72 | 68 |
| inhibit width | **1018 to 1024 us** | **1017 to 1025 us** |
| inhibits carrying a host RTS | **72 of 72** | **68 of 68** |
| **DATA low after CLOCK low** | **255 to 256 us** | **254 to 256 us** |
| CLK runts / fake start bits | 1 / **0** | 1 / **0** |

Textbook: hold CLOCK low, wait, pull DATA low. The spec asks for 100 us before the RTS; both wait
**~255 us, and the whole spread across 140 transactions is under 2 us**. A fixed timer, not best
effort.

**The one difference is edge quality and it does not matter.** Adapter 5 shows 534 sub-200 ns blips
against adapter 4's 12, the widest spread in the campaign. They measure **2 to 8 ns**, one to four
samples at 500 MS/s, and arrive in exactly balanced pairs (267 high, 267 low) with 410 of them
inside a frame on real clock edges. That is threshold flicker on a switching edge, and at this
scale the shifter is in the measurement, so it is not necessarily the adapter's. Either way the PIO
samples a thousand times wider. **The dirtiest edges in the campaign cost nothing.**

> **Anything validated only on these two is not validated.** Every PIO defence is dormant here: the
> inhibit handler runs about five times a second and the idle re-arm has nothing to clean up. Test
> on the from-hell or the test means nothing.

## 4.7 Why they behave like this. INFERENCE, not measurement

Nobody has opened one or read its firmware. Offered because "these adapters are broken" is the
wrong model and leads to the wrong fixes.

**Most of it is legal.** The spec lets the host inhibit whenever it likes and requires the device to
buffer. Holding the bus 56% of the time is a legal mechanism used to excess, and the decisive
measurement is that **not one frame of 2372 was clipped**. It arbitrates correctly. It is not
broken, it is busy.

**Probably it cannot listen and talk at once.** A cheap MCU doing bit-banged PS/2 towards the
keyboard and USB towards the host cannot watch clock edges while servicing USB, and the
protocol-correct answer is exactly what it does. 16.67 ms is 1/60 s to four digits, which smells
like an internal timer tick rather than anything on the wire.

**Why nobody noticed.** The keyboard's 16 byte buffer and retransmission absorb it and the host
never sees a thing. What was never designed for is a third party listening on the wire with neither
the buffer nor the patience.

**Adapters 4 and 5 are one controller and one firmware behind two labels.**

**Harder to call legal:** the truncated final clock, and adapter 2's 1.25 us runt, which is
electrical rather than protocol. Both are the adapter's, seen on the original 5 V lines before the
implant existed. A plausible cause for the runt is an open-drain release artefact in the adapter's
output stage; the analog capture that would confirm it has not been done.

---

# 5. Rules of the bench

**5.1 Never type without Notepad focused.** The chain terminates at this PC, so everything typed
lands in whatever window has focus, including the terminal. Scripts force Notepad forward with
`AttachThreadInput` (Windows refuses `SetForegroundWindow` to a non-foreground process) and **abort
rather than type** if they cannot.

**5.2 Clear the host's caps lock first.** The decoder models caps-off and tracks it from the wire,
so a host that already had caps on can never agree. Scripts read `leds=` from `PS2` and use
`SET CAPSFIX OFF` so the wire carries literal shift.

**5.3 Back up the keylog before `/keylog?clear=1`.** Destructive, and the log holds real history.
Download runs at ~16.8 KB/s in 1400 B chunks at RSSI -41, so a full 900 KB log takes **~55 s**.

**5.4 The PIO is hand-tuned against the adapters in section 4.** Its timings were measured on real
hardware over hours; the repeated re-arm is the glitch defence, and 4.4 shows the inhibit path
load-bearing at 60 Hz. **This rule is a gate on PIO changes, not a standing chore**: it asks for
simulation against `stuff/ps2caps/` first, and that simulator should not be built until a PIO
change is actually on the table (6). Budget is tight: **PIO0 26 of 32, PIO1 23 of 32**.

**5.5 Do not touch the PC at all while a run is typing.** Not the mouse, not the keyboard, **not the
editor**: saving a file in VS Code moves the foreground and the rest of the burst goes wherever it
went. On 2026-08-27 one run aborted cleanly and another **typed the corpus into the agent's own chat
input**. The abort is a backstop, not a guarantee: it can only check focus between bursts. A 20
round bisect is **15 minutes hands off**; plan the window before launching.

---

# 6. The loss, and what it cost to find

## 6.1 What it was

**One stale GPIO read.** `wait_esp_ready()` tests a level, and SLAVEREADY can still be high from
the *previous* transaction when the drain loop comes back round, so a record gets clocked at an ESP
with nothing armed. `spi_write_blocking` succeeds whether or not anyone is listening, and with no
transaction there is no callback, so **no counter moved on either side** while the cursor advanced
anyway. A capture missing a byte looked exactly like a key never pressed.

The fix is four mechanisms, each documented where it lives: sequenced records so a re-send is
idempotent, unconditional dual send, advance only on acknowledgement or on the ESP's published
sequence, and reconciliation that rewinds the ring to refill a hole. **The guards on the rewind are
not optional and each is commented with the measurement that forced it.** Start at
[com.h](firmware/com/com.h).

`/stats` gained `spi_gaps` (sequences that never arrived, **the first true loss counter this
firmware ever had**), `spi_refused` (holes detected) and `spi_dups` (recovery working). Healthy
traffic: refused and dups climbing, gaps at zero. `spi_gaps` matched the analyzer exactly on every
check: 8, 3, 6, 0.

## 6.2 The result

| | 1 from hell | 2 Aliexpress |
|---|---|---|
| bytes on the wire | 2631 | 1983 |
| holes found / refilled | 12 / 2 | 14 / 2 |
| `spi_gaps` left unfilled | **0** | **0** |
| **verdict** | **identical byte for byte** | **identical byte for byte** |

Same byte sequence in the same order, with adjacent-identical counts matching, which is what rules
out a duplicate.

| Build | Records | Lost | Duplicated |
|---|---|---|---|
| original | 859 | 5 (0.58%) | 0 |
| ack + retry, 10 ms | 745 | 0 | **1** |
| ack + retry, 200 ms | 1853 | 9 (0.49%) | 0 |
| sequence + ack | 2872 | 8 (0.28%) | 0 |
| sequence + dual send, pooled | 5537 | 9 (0.16%) | 0 |
| **+ reconciliation** | 2631 | **0** | **0** |

**Being more patient made it strictly worse.** At 200 ms the late fall always arrives, the retry
never fires, and the loss goes back to being silent. The timeout is not "how patient to be", it is
"how late a fall stops looking like ours".

**OTA and both rollbacks re-validated** against the new drain, since it can now block on a record
the ESP refuses. RP OTA, ESP OTA, `.pkg` for both chips, and captures after each read identical to
the wire. ESP rollback fired at 5 minutes exactly; forcing it needs **total** HTTP silence, since
any request confirms the image. RP rollback landed on golden on the fourth boot.

## 6.3 Traps of method, each one paid for

**Sample sizes here are far smaller than they look.** At 0.2% with 126 bytes a round that is a
quarter of a lost byte per round, so a condition needs about **100 rounds** to catch a 2x
difference five times in six, and **150** to catch it nineteen times in twenty. Two died to this: a
per-corpus table where 0% and 1.32% were the same rate, and a bisect whose 7x effect vanished on
the next run.

**A measurement can be an artefact of the harness.** Three separate ones: the analyzer's 2/1 floor
and edge-trimmed partial frames (3.2), `GET /rp` freezing (2.2), and an RTS detector looking for
DATA low *after* an inhibit instead of *during* it, which inflated 96 real host bytes into 298.

**Align the capture against the expected stream to learn WHERE a byte died.** Rebuild the expected
scancodes (`a` is `1C F0 1C`, `A` is `12 1C F0 1C F0 12`) and read the timestamps either side of
each hole. That showed holes are **non-contiguous inside a character pair**, ruling out a dropped
SPI frame or a stalled drain: frames were dying one at a time.

## 6.4 Exonerated, and staying that way

**The capture PIO**: 14 sessions, never short (4.3). **The idle re-arm race**: predicted ~0.8% and
looked like a match, was not the mechanism. **The inhibit path**: fired ~3550 times a minute for
nothing. **Everything from the ESP onwards**: in every round that lost a byte the ESP's count was
short by exactly what the log was short by, so it never received it.

Two fixes for the idle race that do **not** work, so they are not re-attempted: re-checking the
lines in the ISR (during a frame the clock is high half the time, so "both high" passes often
enough to keep killing bytes) and checking the SM's program counter (a desynchronised SM and a
capturing SM both sit past the start-bit wait).

---

# 7. Open items

**Adapters 4 and 5 with the final build.** Should be a formality, which is what this file exists to
disprove.

**The USB variant: ported, compiled, NEVER RUN.** Mirrors all four mechanisms in
[firmware/usb/rp/okhi.c](firmware/usb/rp/okhi.c). First place to look if it misbehaves: its report
ring is **64 entries, not 2000**, and drops the NEWEST when full, so the rewind is capped at
`USB_REWIND_MAX`, a quarter of the ring, **and that fraction is a guess** from a different ring and
a different traffic pattern.

**`/rpcommit` needed a second try once: fixed, cause NOT proven.** It was carried by a budget of 64
frames while `/rpreset` and `/rpbootsel` use a 10 s time window; it is now time-budgeted too and
verified against a busy bus. **But the arithmetic does not support that story**: 64 frames is about
3 seconds of budget and the RP polls every 50 ms. A real fragility was removed; the original
failure is still unexplained. If a `/rpcommit` ever again answers 200 and never arrives, the cause
was something else.

**One record reached the ESP and never reached flash.** First and only time. Probably the tail of a
burst still in the ring at download time, but it had never happened before.

**The offline simulator: do NOT build it yet.** Six changes were attempted on 2026-08-27 and five
were wrong, and **not one was in the PIO** (two statistics errors, two code-reading errors, one
that depended on the ESP's transmit queue depth). `stuff/ps2caps/` holds PS/2 **bus** waveforms:
they would feed a simulator of the one part that has never failed. It becomes worth building the
moment anyone touches the PIO, and two candidate changes are already written down: a one-shot idle
detector, and a capture SM that rejects an over-long start bit by itself.

**Expose the RP's live record counter over SPI.** The production PCB has no serial header, so
oracle 2 is bench-only.
