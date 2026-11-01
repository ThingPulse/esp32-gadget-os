#!/usr/bin/env node
// Validate the real release configuration with isolated, distinct fixture binaries.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp, mkdir, readFile, writeFile, cp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { prepare } from './app-market-release.mjs';

const repo = fileURLToPath(new URL('../', import.meta.url));
const root = await mkdtemp(join(tmpdir(), 'ota-manifest-test-'));
try {
  const config = JSON.parse(await readFile(join(repo, '.app-market/config.json'), 'utf8'));
  await mkdir(join(root, '.app-market'), { recursive: true });
  await writeFile(join(root, '.app-market/config.json'), JSON.stringify(config));
  for (const target of config.targets) {
    for (const ref of [target.icon, ...target.partitions]) {
      const path = join(root, ref.source);
      await mkdir(dirname(path), { recursive: true });
      await writeFile(path, ref.source); // Different firmware content for each board.
    }
  }
  await cp(join(repo, 'docs/releases'), join(root, 'docs/releases'), { recursive: true });
  await prepare(root, 'v0.2.0-test');
  const manifest = JSON.parse(await readFile(join(root, '.app-market/release/ota-manifest.json'), 'utf8'));
  assert.deepEqual(Object.keys(manifest.targets).sort(), ['esp32-color-kit', 'esp32-color-kit-pro']);
  for (const board of ['esp32-color-kit', 'esp32-color-kit-pro']) {
    const configTarget = config.targets.find(t => t.environment === board);
    const firmware = configTarget.partitions.find(p => p.name === 'Firmware');
    const bytes = await readFile(join(root, firmware.source));
    assert.equal(manifest.targets[board].url,
      `https://github.com/ThingPulse/esp32-gadget-os/releases/download/v0.2.0-test/${firmware.asset}`);
    assert.equal(manifest.targets[board].size, bytes.length);
    assert.equal(manifest.targets[board].sha256, createHash('sha256').update(bytes).digest('hex'));
  }
  assert.notEqual(manifest.targets['esp32-color-kit'].sha256, manifest.targets['esp32-color-kit-pro'].sha256);
  console.log('Grande and Pro OTA manifests select distinct, correctly hashed firmware assets.');
} finally {
  await rm(root, { recursive: true, force: true });
}
