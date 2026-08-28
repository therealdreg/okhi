# TODO: USB Full Speed capture by Dreg

Notes for a future attempt at capturing **USB Full Speed (12 Mbit/s)** as well as the Low
Speed (1.5 Mbit/s) this firmware handles today. Written after measuring the current decoder
rather than guessing, so the numbers below are worth trusting more than intuition.

**Short version:** sampling Full Speed is not the problem and needs no overclock. The problem
is that the software decoder costs about 26 CPU cycles per bit, and a Full Speed bit lasts
83 ns. Fixing that means decoding through a lookup table instead of bit by bit.

---

## 1. Sampling is already fast enough

The PIO sampling loop is cycle counted to take exactly **10 PIO cycles per captured bit**.
That ratio holds for both speeds, the only thing that changes is the clock divider:

| speed | bit time | PIO clock | divider from 120 MHz | cycles/bit |
|---|---|---|---|---|
| Low Speed, 1.5 Mbit/s | 666.67 ns | 15 MHz | 8 | 10 |
| Full Speed, 12 Mbit/s | 83.33 ns | 120 MHz | 1 | 10 |

Both dividers are exact integers at `clk_sys = 120 MHz`, so there is no fractional divider
jitter in either case. **The `.pio` sampling loop does not need rewriting**, and running the
RP2040 at 240 MHz buys nothing here: it would just mean a divider of 2 for the same result.

The upstream this port came from, [usb-sniffer-lite](https://github.com/ataradov/usb-sniffer-lite),
already captures Full Speed at exactly this clock:

```c
PIO0->SM0_CLKDIV = ((g_buffer_info.fs ? 1 : 8) << PIO0_SM0_CLKDIV_INT_Pos);
#define F_CPU 120000000
```

## 2. What is already in the tree

More than you would expect. The `fs` flag survived the port and still drives real decisions:

| where | what it does |
|---|---|
| `okhi.c` `process_packet()` | expects SYNC `0x80` at Full Speed instead of `0x81` |
| `okhi.c` `process_buffer()` | discards single-bit keep-alives, which only Low Speed uses as frame marks |
| `okhi.c` `start_time()` | divides the bit count by 12 or by 1.5 to reconstruct timing |
| `okhi.c` display | prints "FS packet" or "LS packet" |

So the decode and display side already understands both speeds. Nothing there has to be
invented, only exercised.

## 3. What is actually missing

**A Full Speed PIO program.** At Full Speed the J/K line polarity is reversed: J is D+ high,
whereas at Low Speed J is D- high. The sampler therefore has to wait on and branch off the
opposite line. From `okhi.pio`:

```
;      IN base + 0 : D+   ...  JMP PIN : D+ (low speed; a full-speed build would use D-)
    wait 1 PIN 1             ; Wait for D- high = bus idle (J) at low speed.
                             ; (Full speed would wait on D+ / PIN 0 instead.)
```

Two ways to do it, both used in the wild: load a second program and pick one at init, or
patch the three affected instructions at run time. The instruction budget is tight (PIO0 must
stay exactly 32 instructions because it relies on the PC rolling 31 to 0), so a second
program in the other state machine is probably cleaner than trying to make one program do
both.

## 4. The real bottleneck: the decoder

This is the part that decides whether the idea works at all, and the reason okhi is a harder
case than the upstream. **usb-sniffer-lite fills RAM and then displays it**, a one shot
capture. okhi runs a continuous loop: core 0 captures into one ping-pong buffer while core 1
decodes the other and forwards the result to the ESP over SPI. Nothing may be dropped.

Disassembling `process_packet()` from the current build:

- the whole function is **313 instructions**
- the inner loop, the one that runs **once per bit**, is **17 to 24 instructions**
- on Cortex-M0+ at roughly 1.3 cycles per instruction that is about **26 cycles per bit**,
  call it 217 ns at 120 MHz

Against the bit budget:

| | bit time | decode cost | verdict |
|---|---|---|---|
| Low Speed | 666 ns | ~217 ns | 3x headroom, comfortable |
| Full Speed at 120 MHz | 83 ns | ~217 ns | **2.6x too slow** |
| Full Speed at 240 MHz | 83 ns | ~108 ns | **still 1.3x too slow** |

**Overclocking does not rescue this.** It helps the decoder, not the sampler, and even at
double the clock the decoder still cannot keep up with sustained traffic.

What saves it in practice is duty cycle: USB is not busy 100% of the time. A real capture on
this hardware shows 3335 frames per buffer of which 3334 are empty, almost all short IN/NAK
polling. Average load is low. But **bursts are the risk**: a sustained run of back to back
packets is what would overflow the ping-pong and lose keystrokes, and losing keystrokes
silently is the worst failure this device can have.

### Why the loop is per bit at all

NRZI on its own is trivially parallel. A decoded bit is 1 when there was no transition, so a
whole word decodes in three operations:

```c
decoded = ~(w ^ (w >> 1));
```

What forces the per bit loop is **bit stuffing**. After six consecutive 1s the transmitter
inserts a 0 that has to be removed, so the output length is variable and depends on state
carried across words. That is what the `cmp r4, #6` in the inner loop is doing.

## 5. The fix: decode through a lookup table

Process **8 bits per iteration instead of 1**, using a table precomputed for every
combination of input byte and stuffing state.

```
index:  (input byte, number of consecutive 1s carried in, 0..6)
value:  destuffed output bits | how many bits came out | new stuffing state
```

Size: 256 input bytes x 7 states = **1792 entries**, roughly 3 bytes each, about **5.3 KiB**.
The `usb/rp` build currently reports 203044 B of the 262144 B the linker gives it, leaving
about **57 KiB free**, so there is room. The table should live in RAM rather than XIP flash so
lookups do not stall on the flash cache.

Expected result:

| | cycles/bit | Full Speed margin |
|---|---|---|
| today, bit by bit | ~26 | 2.6x too slow |
| with an 8 bit LUT | ~3.3 | **3x headroom** |

That lands Full Speed comfortably inside budget **at 120 MHz, with no overclock**.

### Why not something cleverer

On a Cortex-M3 or M4 you could use `CLZ` to jump straight to the next run of six 1s and skip
most of the work. The **RP2040 is Cortex-M0+ (ARMv6-M) and has no `CLZ` instruction**, so
that door is closed. The table is effectively the only lever.

## 6. How to validate it

The decoder is the heart of this product and a subtly wrong table does not crash, it just
returns the wrong keys now and then. So:

1. Generate the table with a script, committed alongside the firmware, never by hand.
2. Build a differential test that runs **the old decoder and the new one over the same
   captured buffers** and asserts they agree bit for bit. This runs on a PC, no board needed:
   the decoder is plain C over an array.
3. Feed it real captures, plus synthesised worst cases: long runs of 1s that force stuffing at
   every word boundary, packets ending mid stuffing, runt and malformed packets.
4. Only once they agree over millions of packets, switch the firmware over.
5. Keep the old path compiled in behind a flag for one release, so a regression can be
   bisected against it.

Note that `process_packet()` has recently gained two record framing fixes (short packet exits
now write the record header, and the read cursor is bounded). Any rewrite has to preserve
both, since the consumer strides `2 + ceil(size/4)` words from the header word and a
mismatch there loses the rest of the buffer.

## 7. Is it worth doing

Honest answer: **probably not, for keyboards.** Almost every USB keyboard is Low Speed. Full
Speed shows up on gaming boards with high polling rates, keyboards with an integrated hub,
and units carrying RGB or audio. For the device's main purpose the return is small.

It is worth doing if the goal changes: capturing mice with high report rates, or using okhi
as a general purpose USB sniffer rather than a keylogger. In that case the LUT decoder is
the enabling piece, and it would speed up Low Speed capture too, leaving far more headroom
for everything else core 1 has to do.

Suggested order if it is ever attempted:

1. Write the LUT decoder and the differential test first, on the PC. **This is the risky part
   and it can be done with no hardware at all.**
2. Measure the real cost on the board at Low Speed, where a regression is harmless because
   the current margin is 3x.
3. Only then port the Full Speed PIO program and try a real Full Speed device.

Doing it in that order means the dangerous change is validated while the firmware is still
doing its normal job.
