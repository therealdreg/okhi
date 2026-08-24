#!/usr/bin/env node

// by Dreg

// npm install html-minifier
const fs = require('fs');
const vm = require('vm');
const zlib = require('zlib');
const crypto = require('crypto');
const path = require('path');
const { minify } = require('html-minifier');

const INPUT = 'index.html';

// The page is EMBEDDED in the firmware, so a stale one is invisible: the implant
// reports a healthy ESP and a healthy RP while serving whatever HTML happened to
// be generated last. Editing index.html and forgetting to run this script is the
// whole failure mode. Stamping the moment of generation and a hash of the source
// into the page turns that from a hunt into a glance.
const STAMP_TOKEN = '@@WEBGEN@@';

// Only so this script can say whether the built firmware predates the page it is
// about to write.
const FIRMWARE_BIN = path.join('..', 'firmware', 'usb', 'esp', 'build', 'okhi.bin');

const OUT_MIN = 'webuff.txt';
const OUT_GZ  = 'webuff_gz.txt';
const OUT_ORI = 'webuff_orig.txt';

// The page is EMBEDDED in the ESP application, it no longer lives in SPIFFS.
// main/CMakeLists.txt picks this file up with EMBED_FILES, which is why it has
// to land in the component directory and keep exactly this name: the linker
// derives _binary_index_html_gz_start from it.
const EXTRA_PATH_GZ_EMBED = '..\\firmware\\usb\\esp\\main\\index.html.gz';
const EXTRA_MIN = '..\\firmware\\usb\\esp\\src\\webuff.txt';
const EXTRA_GZ  = '..\\firmware\\usb\\esp\\src\\webuff_gz.txt';
const EXTRA_ORI = '..\\firmware\\usb\\esp\\src\\webuff_orig.txt';

function toCByteArray(buffer, varName, withProgmem = true) {
  const decl = `const uint8_t ${varName}[] ${withProgmem ? 'PROGMEM ' : ''}= {\n`;
  let out = decl;
  const perLine = 16;
  for (let i = 0; i < buffer.length; i++) {
    if (i % perLine === 0) out += '  ';
    out += '0x' + buffer[i].toString(16).padStart(2, '0');
    out += (i === buffer.length - 1) ? '' : ', ';
    if ((i + 1) % perLine === 0) out += '\n';
  }
  out += `\n};\nconst size_t ${varName}_len = sizeof(${varName});\n`;
  return out;
}

// A single missing comma once shipped a page whose whole 72 KB script died at
// parse time. Nothing looked wrong: the HTML is static, so the page rendered,
// every panel stayed empty and every button was dead, and the only way to find
// it was to flash the firmware and open devtools. The parser is free and it is
// the same V8 the browser uses, so run it here and refuse to emit a page that
// cannot execute. This check is the reason a broken page can no longer reach
// the firmware.
function collectInlineScripts(html) {
  const scripts = [];
  const re = /<script(\s[^>]*)?>([\s\S]*?)<\/script>/gi;
  let match;

  while ((match = re.exec(html)) !== null) {
    const attrs = match[1] || '';

    // Anything with a src is not ours to parse, and a non JS type is data.
    if (/\ssrc\s*=/i.test(attrs)) continue;
    if (/\stype\s*=/i.test(attrs) && !/\stype\s*=\s*["']?(text\/javascript|module)["']?/i.test(attrs)) continue;

    const body = match[2];
    const before = html.slice(0, match.index + match[0].indexOf(body));

    scripts.push({
      code: body,
      // Line the script body starts on, so a parse error can be reported
      // against index.html rather than against an extracted fragment.
      startLine: before.split('\n').length - 1,
    });
  }

  return scripts;
}

function lintScripts(html) {
  const failures = [];

  for (const script of collectInlineScripts(html)) {
    try {
      // lineOffset makes V8 count from the position in index.html, so the
      // number printed is the one to jump to in the editor.
      new vm.Script(script.code, { filename: INPUT, lineOffset: script.startLine });
    } catch (error) {
      failures.push(error);
    }
  }

  return failures;
}

// getElementById against an id the HTML does not define returns null, and the
// dereference right after it throws at run time, taking out whatever else that
// function was going to do. Cheap to catch here.
function lintElementIds(html) {
  const declared = new Set();
  const declRe = /\sid\s*=\s*["']([^"']+)["']/gi;
  let match;

  while ((match = declRe.exec(html)) !== null) {
    declared.add(match[1]);
  }

  // Elements the script builds itself never appear in the markup, so take an
  // assignment to .id as a declaration too or every one of them reads as missing.
  const dynRe = /\.id\s*=\s*["']([^"']+)["']/g;

  while ((match = dynRe.exec(html)) !== null) {
    declared.add(match[1]);
  }

  const missing = new Set();
  const useRe = /getElementById\(\s*['"]([^'"]+)['"]\s*\)/g;

  while ((match = useRe.exec(html)) !== null) {
    if (!declared.has(match[1])) missing.add(match[1]);
  }

  return [...missing];
}

const data = fs.readFileSync(INPUT, 'utf8');

console.log('\n--- Lint ---');

const parseFailures = lintScripts(data);

if (parseFailures.length > 0) {
  console.error('\n' + INPUT + ' does not parse, so NOTHING was written.\n');

  for (const error of parseFailures) {
    // The first stack line carries file:line, the caret line points at the column.
    const head = String(error.stack || error.message).split('\n').slice(0, 4).join('\n');
    console.error(head + '\n');
  }

  console.error('A parse error kills the ENTIRE script: the page still renders, but no');
  console.error('panel fills in and no button responds. V8 often points at the line AFTER');
  console.error('the real mistake, so check the line above it too, a missing comma at the');
  console.error('end of an object entry is the usual cause.\n');
  process.exit(1);
}

console.log('script parses clean');

const missingIds = lintElementIds(data);

if (missingIds.length > 0) {
  console.log('WARNING: getElementById for ids the page never declares: ' + missingIds.join(', '));
} else {
  console.log('every getElementById target exists');
}

const origBuf = Buffer.from(data, 'utf8');

// Hashed BEFORE substitution, so the same source always produces the same hash
// no matter when it was generated. That is what makes it comparable: sha256
// the working copy and check it against the WEB line the implant is showing.
const srcHash = crypto.createHash('sha256').update(origBuf).digest('hex').slice(0, 12);

// Read the page the firmware is currently built around BEFORE overwriting it.
// Comparing its stamp against the source hash is what makes the staleness
// check able to say yes as well as no: a timestamp alone can only ever report
// that the file just written is newer than the binary, which it always is.
let prevSha = null;
let prevGzTime = 0;

try {
  prevGzTime = fs.statSync(EXTRA_PATH_GZ_EMBED).mtimeMs;
  const prev = zlib.gunzipSync(fs.readFileSync(EXTRA_PATH_GZ_EMBED)).toString('utf8');
  const stampMatch = prev.match(/WEBGEN_STAMP = '([^']*)'/);
  const shaMatch = stampMatch && stampMatch[1].match(/sha ([0-9a-f]+)/);

  if (shaMatch) {
    prevSha = shaMatch[1];
  }
} catch (e) {
  // No previous page, or one generated before stamping existed.
}

const now = new Date();
const stamp = now.toISOString().replace('T', ' ').slice(0, 19) + 'Z  src ' + origBuf.length +
  ' B  sha ' + srcHash + '  node ' + process.version;

if (!data.includes(STAMP_TOKEN)) {
  console.error('\nWARNING: ' + INPUT + ' has no ' + STAMP_TOKEN + ' token, so the page it');
  console.error('generates cannot say when it was built or which source it came from.\n');
}

const shipped = data.split(STAMP_TOKEN).join(stamp);
const shippedBuf = Buffer.from(shipped, 'utf8');

// The token substitution is the last thing that touches the script, so parse the
// result too rather than trusting that it could not have broken anything.
const shippedFailures = lintScripts(shipped);

if (shippedFailures.length > 0) {
  console.error('\nThe stamped page does not parse, so NOTHING was written.');
  console.error('The stamp itself broke it, check STAMP_TOKEN and its quoting.\n');
  console.error(String(shippedFailures[0].stack || shippedFailures[0].message).split('\n').slice(0, 4).join('\n'));
  process.exit(1);
}

const minified = minify(shipped, {
  removeAttributeQuotes: true,
  removeEmptyAttributes: true,
  collapseWhitespace: true,
  removeComments: true,
  removeRedundantAttributes: true,
  removeScriptTypeAttributes: true,
  removeStyleLinkTypeAttributes: true,
  removeTagWhitespace: true,
  useShortDoctype: true,
  minifyJS: true,
  minifyCSS: true,
});
const minBuf = Buffer.from(minified, 'utf8');

console.log('\n--- Sizes ---');
console.log('Build stamp:', stamp);
console.log('Original size:', origBuf.length, 'bytes');
console.log('Minified size:', minBuf.length, 'bytes');
console.log('Saved:', origBuf.length - minBuf.length, 'bytes');

const gzBuf = zlib.gzipSync(shippedBuf, { level: zlib.constants.Z_BEST_COMPRESSION });

console.log('Gzipped (original) size:', gzBuf.length, 'bytes');

// Decompress what is about to be embedded and parse THAT. It is the exact byte
// range the linker puts in the binary and the exact bytes the browser will run,
// so this is the check that actually covers what ships.
const roundTrip = zlib.gunzipSync(gzBuf).toString('utf8');

if (roundTrip !== shipped) {
  console.error('\nThe gzip round trip does not reproduce the page, refusing to write.');
  process.exit(1);
}

if (lintScripts(roundTrip).length > 0) {
  console.error('\nThe page inside the gzip does not parse, refusing to write.');
  process.exit(1);
}

console.log('embedded copy round trips and parses clean');

// Everything is verified, so write. Nothing above this line touches a file,
// which is what keeps a failed lint from leaving half a page behind for the
// next build to pick up.
const cMin = toCByteArray(minBuf, 'web_min');
fs.writeFileSync(OUT_MIN, cMin);
console.log(`${OUT_MIN} written`);
//fs.writeFileSync(EXTRA_MIN, cMin);

const cOri = toCByteArray(shippedBuf, 'web_orig');
fs.writeFileSync(OUT_ORI, cOri);
console.log(`${OUT_ORI} written`);
//fs.writeFileSync(EXTRA_ORI, cOri);

const cGz = toCByteArray(gzBuf, 'web_gz');
fs.writeFileSync(OUT_GZ, cGz);
console.log(`${OUT_GZ} written`);
//fs.writeFileSync(EXTRA_GZ, cGz);

fs.writeFileSync('index.orig.gz', gzBuf);
console.log('index.orig.gz written');

fs.writeFileSync(EXTRA_PATH_GZ_EMBED, gzBuf);
console.log(`${EXTRA_PATH_GZ_EMBED} written`);

const minGzBuf = zlib.gzipSync(minBuf, { level: zlib.constants.Z_BEST_COMPRESSION });

console.log('\n--- Embedded cost ---');
console.log('Shipped inside the ESP application:', gzBuf.length, 'bytes');
console.log('Flash actually consumed:', gzBuf.length * 2, 'bytes, because ota_0 and ota_1 both carry it');
console.log('Minified would be:', minGzBuf.length, 'bytes, a saving of', gzBuf.length - minGzBuf.length);
console.log('\nThe ORIGINAL is what gets shipped. html-minifier 4 runs minifyJS through');
console.log('uglify-js 3, which only parses ES5, and this page uses template literals and');
console.log('arrow functions. Switch to minBuf only if you load the page afterwards and');
console.log('confirm the JS still runs.');
console.log('\nNow rebuild the ESP: the page is linked into the binary, not flashed to SPIFFS.');

// The one check that actually catches the mistake this stamp exists for:
// does the firmware sitting in build/ carry THIS page, or an older one?
try {
  const binTime = fs.statSync(FIRMWARE_BIN).mtimeMs;

  if (prevSha === srcHash && binTime > prevGzTime) {
    console.log('\nThe built firmware already carries this exact page, sha ' + srcHash + '.');
    console.log('Only the timestamp inside it changed, so a rebuild is optional.');
  } else if (prevSha === null) {
    console.log('\n*** The page in build/ predates stamping, so it cannot be compared.');
    console.log('*** Rebuild the ESP to be sure it carries this one.');
  } else {
    console.log('\n*** The firmware in build/ carries a DIFFERENT page, sha ' + prevSha + '.');
    console.log('*** Rebuild the ESP or the implant will serve the old one.');
  }
} catch (e) {
  console.log('\nNo built firmware at ' + FIRMWARE_BIN + ', nothing to compare against.');
}
