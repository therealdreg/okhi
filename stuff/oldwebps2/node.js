#!/usr/bin/env node

// by Dreg

// npm install html-minifier
const fs = require('fs');
const minify = require('html-minifier').minify;

fs.readFile('index.html', 'utf8', (err, data) => {
  if (err) {
    console.error(err);
    return;
  }
  const result = minify(data, {
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

  console.log(result);

  console.log('\n\nOriginal size:', data.length, 'bytes');
  console.log('Minified size:', result.length, 'bytes');
  console.log('Difference:', data.length - result.length, 'bytes');

  const hexString = Buffer.from(result, 'utf8').toString('hex');

  let formattedHexString = '\nPGM_P web =\n{\n    "';
  for (let i = 0; i < hexString.length; i += 2) {
    if (i > 0 && i % 20 === 0) {
      formattedHexString += '"\n    "'; 
    }
    formattedHexString += '\\x' + hexString.substring(i, i + 2);
  }
  formattedHexString += '"\n};';

  console.log(formattedHexString);

  fs.writeFile('webuff.txt', formattedHexString, (err) => {
    if (err) {
      console.error(err);
      return;
    }
    console.log('webuff.txt written');
  });

  fs.writeFile('C:\\Users\\dreg\\Desktop\\tmp\\okhi\\github\\okhi\\firmware\\ps2\\esp\\src\\webuff.txt', formattedHexString, (err) => {
    if (err) {
      console.error(err);
      return;
    }
    console.log('webuff.txt written');
  });
});


