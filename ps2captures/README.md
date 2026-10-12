# PS/2 adapter capture sessions

Raw evidence behind the numbers in [../ps2adapter.md](../ps2adapter.md). One directory per adapter
per run, written by `ps2_scope.py session` (see section 2.3 of that file).

These are kept in the repo on purpose. A session is about 300 KB and **cannot be reproduced**
without the physical adapter, a real PS/2 keyboard and a human typing at it.

## What is in a session directory

| File | What it is |
|---|---|
| `capture.sal` | The Saleae save file. **Open this in Logic 2** to scroll the waveform by hand |
| `digital.csv` | The same capture exported as transitions, which is what the analysis reads |
| `rp_serial.txt` | The RP2040's own serial output during the session. It prints one line per record it drains, so this is the third counting point |
| `keylog.txt` | What the implant actually captured, downloaded from `/keylog` |
| `notepad.txt` | What Windows made of it, read out of Notepad by UI Automation |

Together those are four independent views of the same keystrokes, which is what makes a lost byte
attributable to a stage instead of merely noticed. See section 3.4 of `ps2adapter.md`.

## Re-analysing without re-capturing

```bash
python firmware/leonardo_uart2keyboard/test/ps2_scope.py analyze ps2captures/<session>
```

Everything in the verdict is recomputed from the files, so a fix to the analysis applies
retroactively to every session ever recorded. That is not a nicety: the first session of the
campaign crashed after the waveform was safely on disk, and this is what saved it.

## What the typing looks like, and why it is deliberately awful

The operator mashes keys: long presses that trigger typematic repeat, several keys held at once so
the keyboard exercises rollover, and caps lock spammed from a **second** keyboard so Windows sends
Set-LEDs commands and the host transmits **into** the burst. That last part is the only way to put
host-to-device traffic on the wire, and it is why record counts run far ahead of the character
count in `notepad.txt`: a held key repeats without adding characters, and a caps lock press adds
two records and no character at all.

## Privacy

`notepad.txt` and `keylog.txt` are, by construction, a keystroke log. Every session here is
deliberate key mashing. **If a run ever catches real typing, delete that session before
committing.**

## Sessions

`adapter2_*`, `adapter4_aliexpress_*`, `adapter5_aliexpress_*` and `adapter1_from_hell_amazon_*`
are the campaign proper. `adapter2_instrumented_*` and `adapter2_ack10ms_*` are the two
measurement steps that tuned the acknowledgement timeout, and `adapter2_FIXED_*` is the first
zero-loss session, recorded immediately after flashing the fix. The story is section 7.7b.
