#!/usr/bin/env node

// by Dreg

// Decode an okhi keylog (PS/2 or USB) with okhi's OWN decoder.
//
// The tables and the decoder are lifted verbatim out of the page at run time instead of being
// reimplemented here, so this can never drift from what the web UI actually shows. Which decoder
// runs is decided by the page: webps2/index.html carries the PS/2 scancode tracker,
// webusb/index.html the HID report parser. If the markers move, this exits 2 rather than decoding
// with a stale copy of anything.
//
// usage: node okhi_decode.js <webps2|webusb/index.html> <keylog.txt> [layout]
//        layout defaults to spanish, the other one shipped is english.

const fs = require('fs');

const [pagePath, logPath, layoutArg] = process.argv.slice(2);
const layout = layoutArg || 'spanish';

if (!pagePath || !logPath) {
    console.error('usage: node okhi_decode.js <webps2|webusb/index.html> <keylog.txt> [layout]');
    process.exit(2);
}

const page = fs.readFileSync(pagePath, 'utf8');
const data = fs.readFileSync(logPath, 'utf8');

function slab(start, end) {
    const from = page.indexOf(start);
    const to = page.indexOf(end);
    return (from < 0 || to < 0 || to <= from) ? null : page.slice(from, to);
}

function checkLayout(layouts) {
    if (!layouts[layout]) {
        console.error('layout "' + layout + '" is not in the page, have: ' + Object.keys(layouts).join(', '));
        process.exit(2);
    }
}

let out = '';
let fed = 0;

const ps2 = slab('var PS2_LETTERS = {', 'const tracker = new RemoteKeyboardTracker();');
const usb = slab('var KEY_NAMES = {', 'document.addEventListener("DOMContentLoaded"');

if (ps2) {
    // Everything from the scancode tables to the end of the tracker class, evaluated as is.
    const decoder = new Function(ps2 + '\nreturn { Tracker: RemoteKeyboardTracker, layouts: PS2_LAYOUTS };')();
    checkLayout(decoder.layouts);
    const tracker = new decoder.Tracker();
    tracker.setKeyboardType(layout);
    // The same packet loop decodePersistentLog() runs in the page. Kept identical on purpose.
    for (const packet of data.split(';')) {
        const match = packet.match(/D:(0[xX][0-9A-Fa-f]{1,2})\s+t:/);
        if (!match) {
            continue;
        }
        out += tracker.feed('0x' + match[1].slice(2).toUpperCase());
        fed++;
    }
} else if (usb) {
    // From the key names to the end of keycodeToChar: layouts, report validation and the parser.
    // decodePersistentLog() in the page is parseKeyboardHID() from a fresh held-key state, which
    // is what a freshly built decoder is.
    const decoder = new Function(usb + '\nreturn { parse: parseKeyboardHID, layouts: LAYOUTS, ' +
                                 'setLayout: function (l) { currentLayout = l; } };')();
    checkLayout(decoder.layouts);
    decoder.setLayout(layout);
    out = decoder.parse(data);
    fed = (data.match(/^IN: /gm) || []).length;
} else {
    console.error('no decoder found in ' + pagePath + ': the markers moved, fix this script');
    process.exit(2);
}

if (process.env.OKHI_DECODE_STATS) {
    console.error('records fed: ' + fed);
}
process.stdout.write(out);
