#!/usr/bin/env node

import { readFile, writeFile } from 'node:fs/promises';
import { deflateSync } from 'node:zlib';

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1)
      crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const typeBuffer = Buffer.from(type, 'ascii');
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const checksum = Buffer.alloc(4);
  checksum.writeUInt32BE(crc32(Buffer.concat([typeBuffer, data])));
  return Buffer.concat([length, typeBuffer, data, checksum]);
}

function parsePpm(input) {
  let offset = 0;
  const tokens = [];
  while (tokens.length < 4) {
    while (offset < input.length && /\s/.test(String.fromCharCode(input[offset]))) offset += 1;
    if (input[offset] === 0x23) {
      while (offset < input.length && input[offset] !== 0x0a) offset += 1;
      continue;
    }
    const start = offset;
    while (offset < input.length && !/\s/.test(String.fromCharCode(input[offset]))) offset += 1;
    tokens.push(input.subarray(start, offset).toString('ascii'));
  }
  while (offset < input.length && /\s/.test(String.fromCharCode(input[offset]))) offset += 1;
  const [magic, widthText, heightText, maxText] = tokens;
  if (magic !== 'P6' || maxText !== '255') throw new Error('Expected an 8-bit binary PPM (P6)');
  const width = Number(widthText);
  const height = Number(heightText);
  const pixels = input.subarray(offset);
  if (pixels.length !== width * height * 3) throw new Error('Unexpected PPM pixel-data size');
  return { width, height, pixels };
}

async function main() {
  const [, , source, destination] = process.argv;
  if (!source || !destination) throw new Error('Usage: ppm-to-png.mjs SOURCE.ppm DESTINATION.png');
  const { width, height, pixels } = parsePpm(await readFile(source));
  const scanlines = Buffer.alloc(height * (1 + width * 3));
  for (let row = 0; row < height; row += 1) {
    const destinationOffset = row * (1 + width * 3);
    scanlines[destinationOffset] = 0;
    pixels.copy(scanlines, destinationOffset + 1, row * width * 3, (row + 1) * width * 3);
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 2;
  const png = Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(scanlines, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
  await writeFile(destination, png);
}

main().catch(error => {
  console.error(error.message);
  process.exitCode = 1;
});
