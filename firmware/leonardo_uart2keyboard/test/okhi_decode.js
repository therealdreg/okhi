#!/usr/bin/env node

// by Dreg

// Decode an okhi PS/2 keylog with okhi's OWN decoder.
//
// The tables and the tracker are lifted verbatim out of webps2/index.html at run time
// instead of being reimplemented here, so this can never drift from what the web UI
// actually shows. If the markers move, this exits 2 rather than decoding with a stale
// copy of anything.
//
// usage: node okhi_decode.js <webps2/index.html> <keylog.txt> [layout]
//        layout defaults to spanish, the other one shipped is english.

const fs = require('fs');

const [pagePath, logPath, layoutArg] = process.argv.slice(2);
const layout = layoutArg || 'spanish';

if (!pagePath || !logPath) {
    console.error('usage: node okhi_decode.js <webps2/index.html> <keylog.txt> [layout]');
    process.exit(2);
}

const START = 'var PS2_LETTERS = {';
const END = 'const tracker = new RemoteKeyboardTracker();';

const page = fs.readFileSync(pagePath, 'utf8');
const from = page.indexOf(START);
const to = page.indexOf(END);
if (from < 0 || to < 0 || to <= from) {
    console.error('decoder not found in ' + pagePath + ': the markers moved, fix this script');
    process.exit(2);
}

// Everything from the scancode tables to the end of the tracker class, evaluated as is.
const slab = page.slice(from, to);
const build = new Function(slab + '\nreturn { Tracker: RemoteKeyboardTracker, layouts: PS2_LAYOUTS };');
const decoder = build();

if (!decoder.layouts[layout]) {
    console.error('layout "' + layout + '" is not in PS2_LAYOUTS, have: ' +
                  Object.keys(decoder.layouts).join(', '));
    process.exit(2);
}

const tracker = new decoder.Tracker();
tracker.setKeyboardType(layout);

// The same packet loop decodePersistentLog() runs in the page. Kept identical on purpose.
const data = fs.readFileSync(logPath, 'utf8');
let out = '';
let fed = 0;
for (const packet of data.split(';')) {
    const match = packet.match(/D:(0[xX][0-9A-Fa-f]{1,2})\s+t:/);
    if (!match) {
        continue;
    }
    out += tracker.feed('0x' + match[1].slice(2).toUpperCase());
    fed++;
}

if (process.env.OKHI_DECODE_STATS) {
    console.error('records fed: ' + fed);
}
process.stdout.write(out);
