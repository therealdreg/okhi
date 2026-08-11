#!/usr/bin/env node

// by Dreg

// npm install html-minifier
const fs = require('fs');
const zlib = require('zlib');
const { minify } = require('html-minifier');

const INPUT = 'index.html';
const OUT_TXT = 'webuff.txt';              
const OUT_GZ_TXT = 'webuff_gz.txt';        
const OUT_ORIG_TXT = 'webuff_orig.txt';    

// The page is EMBEDDED in the ESP application, it no longer lives in SPIFFS.
// main/CMakeLists.txt picks this file up with EMBED_FILES, which is why it has
// to land in the component directory and keep exactly this name: the linker
// derives _binary_index_html_gz_start from it.
const EXTRA_PATH_GZ_EMBED = '..\\firmware\\ps2\\esp\\main\\index.html.gz';
const EXTRA_PATH_TXT = '..\\firmware\\ps2\\esp\\src\\webuff.txt';
const EXTRA_PATH_GZ_TXT = '..\\firmware\\ps2\\esp\\src\\webuff_gz.txt';
const EXTRA_PATH_ORIG_TXT = '..\\firmware\\ps2\\esp\\src\\webuff_orig.txt';

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

  const minC = toCByteArray(minBuf, 'web_min');
  fs.writeFile(OUT_TXT, minC, e => e ? console.error(e) : console.log(`${OUT_TXT} written`));
  //fs.writeFile(EXTRA_PATH_TXT, minC, e => e ? console.error(e) : console.log(`${EXTRA_PATH_TXT} written`));

  const origC = toCByteArray(origBuf, 'web_orig');
  fs.writeFile(OUT_ORIG_TXT, origC, e => e ? console.error(e) : console.log(`${OUT_ORIG_TXT} written`));
  //fs.writeFile(EXTRA_PATH_ORIG_TXT, origC, e => e ? console.error(e) : console.log(`${EXTRA_PATH_ORIG_TXT} written`));

  zlib.gzip(origBuf, { level: zlib.constants.Z_BEST_COMPRESSION }, (gzErr, gzBuf) => {
    if (gzErr) {
      console.error(gzErr);
      return;
    }
    console.log('Gzipped (original) size:', gzBuf.length, 'bytes');

    const gzC = toCByteArray(gzBuf, 'web_gz');
    fs.writeFile(OUT_GZ_TXT, gzC, e => e ? console.error(e) : console.log(`${OUT_GZ_TXT} written`));
    //fs.writeFile(EXTRA_PATH_GZ_TXT, gzC, e => e ? console.error(e) : console.log(`${EXTRA_PATH_GZ_TXT} written`));

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
