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

const EXTRA_PATH_GZ_SPIFF = '..\\firmware\\ps2\\esp\\spiffs\\index.html.gz';
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

    fs.writeFile(EXTRA_PATH_GZ_SPIFF, gzBuf, e => e ? console.error(e) : console.log(`${EXTRA_PATH_GZ_SPIFF} written`));
  });
});
