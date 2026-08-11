#!/usr/bin/env node

// by Dreg

// npm install html-minifier
const fs = require('fs');
const zlib = require('zlib');
const { minify } = require('html-minifier');

const INPUT = 'index.html';

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

fs.readFile(INPUT, 'utf8', (err, data) => {
  if (err) {
    console.error(err);
    return;
  }

  const origBuf = Buffer.from(data, 'utf8');

  const minified = minify(data, {
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
  console.log('Original size:', origBuf.length, 'bytes');
  console.log('Minified size:', minBuf.length, 'bytes');
  console.log('Saved:', origBuf.length - minBuf.length, 'bytes');

  const cMin = toCByteArray(minBuf, 'web_min');
  fs.writeFile(OUT_MIN, cMin, e => e ? console.error(e) : console.log(`${OUT_MIN} written`));
  //fs.writeFile(EXTRA_MIN, cMin, e => e ? console.error(e) : console.log(`${EXTRA_MIN} written`));

  const cOri = toCByteArray(origBuf, 'web_orig');
  fs.writeFile(OUT_ORI, cOri, e => e ? console.error(e) : console.log(`${OUT_ORI} written`));
  //fs.writeFile(EXTRA_ORI, cOri, e => e ? console.error(e) : console.log(`${EXTRA_ORI} written`));

  zlib.gzip(origBuf, { level: zlib.constants.Z_BEST_COMPRESSION }, (gzErr, gzBuf) => {
    if (gzErr) {
      console.error(gzErr);
      return;
    }
    console.log('Gzipped (original) size:', gzBuf.length, 'bytes');

    const cGz = toCByteArray(gzBuf, 'web_gz');
    fs.writeFile(OUT_GZ, cGz, e => e ? console.error(e) : console.log(`${OUT_GZ} written`));
    //fs.writeFile(EXTRA_GZ, cGz, e => e ? console.error(e) : console.log(`${EXTRA_GZ} written`));

    fs.writeFile('index.orig.gz', gzBuf, e => e ? console.error(e) : console.log('index.orig.gz written'));

    fs.writeFile(EXTRA_PATH_GZ_EMBED, gzBuf, e => e ? console.error(e) : console.log(`${EXTRA_PATH_GZ_EMBED} written`));

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
  });
});
