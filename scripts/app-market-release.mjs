#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { copyFile, mkdir, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { basename, isAbsolute, join, relative, resolve } from 'node:path';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const CONFIG_FILE = '.app-market/config.json';
const STAGING_DIRECTORY = '.app-market/release';

function fail(message) {
  throw new Error(message);
}

function normalizeTag(tag) {
  const normalized = String(tag || '').trim().replace(/^v/, '');
  if (!/^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/.test(normalized)) {
    fail(`Invalid release tag: ${tag || '<missing>'}`);
  }
  return normalized;
}

function validateAssetName(name, label) {
  if (!name || basename(name) !== name || name.includes('/') || name.includes('\\')) {
    fail(`${label} must be a release asset basename without a path: ${name}`);
  }
}

function parseOffset(value) {
  if (Number.isInteger(value) && value >= 0) return value;
  if (typeof value === 'string' && /^0x[0-9a-f]+$/i.test(value)) return Number.parseInt(value.slice(2), 16);
  fail(`Invalid flash offset: ${value}`);
}

function resolveInside(root, path, label) {
  if (!path || isAbsolute(path)) fail(`${label} must be a repository-relative path`);
  const resolved = resolve(root, path);
  const fromRoot = relative(root, resolved);
  if (fromRoot.startsWith('..') || isAbsolute(fromRoot)) fail(`${label} escapes the repository: ${path}`);
  return resolved;
}

async function sha256(path) {
  const content = await readFile(path);
  return createHash('sha256').update(content).digest('hex');
}

async function loadConfig(root) {
  const path = join(root, CONFIG_FILE);
  let config;
  try {
    config = JSON.parse(await readFile(path, 'utf8'));
  } catch (error) {
    fail(`Cannot read ${CONFIG_FILE}: ${error.message}`);
  }
  if (config.schemaVersion !== 1) fail('Config schemaVersion must be 1');
  const targets = Array.isArray(config.targets)
    ? config.targets
    : [{ app: config.app, icon: config.icon, partitions: config.partitions, manifestAsset: 'app-market.json' }];
  if (targets.length === 0) fail('At least one target is required');
  const ids = new Set();
  const outputSources = new Map();
  for (const target of targets) {
    if (!/^[a-z0-9][a-z0-9-]*$/.test(target.app?.id || '')) fail('Invalid target app.id');
    if (ids.has(target.app.id)) fail(`Duplicate target app.id: ${target.app.id}`);
    ids.add(target.app.id);
    if (!target.app.name || !target.app.description) fail(`${target.app.id}: app.name and app.description are required`);
    if (!Array.isArray(target.app.supportedDevices) || target.app.supportedDevices.length === 0) {
      fail(`${target.app.id}: at least one supported device ID is required`);
    }
    if (!Array.isArray(target.partitions) || target.partitions.length === 0) fail(`${target.app.id}: at least one partition is required`);
    validateAssetName(target.manifestAsset, `${target.app.id}.manifestAsset`);
    validateAssetName(target.icon?.asset, `${target.app.id}.icon.asset`);
    const references = [target.icon, ...target.partitions];
    const targetAssets = new Set();
    for (const partition of target.partitions) validateAssetName(partition.asset, `${target.app.id}.${partition.name}.asset`);
    for (const reference of references) {
      if (targetAssets.has(reference.asset)) fail(`${target.app.id}: release asset filenames must be unique`);
      targetAssets.add(reference.asset);
      const priorSource = outputSources.get(reference.asset);
      if (priorSource && priorSource !== reference.source) {
        fail(`Release asset ${reference.asset} is produced by more than one source`);
      }
      outputSources.set(reference.asset, reference.source);
    }
    if (outputSources.has(target.manifestAsset)) fail(`Manifest filename conflicts with a release asset: ${target.manifestAsset}`);
    outputSources.set(target.manifestAsset, `<manifest:${target.app.id}>`);
  }
  return { ...config, targets };
}

async function stageFile(root, staging, reference, label) {
  const source = resolveInside(root, reference.source, `${label}.source`);
  const destination = join(staging, reference.asset);
  try {
    const info = await stat(source);
    if (!info.isFile()) fail(`${label} source is not a file: ${reference.source}`);
    await copyFile(source, destination);
  } catch (error) {
    if (error.code === 'ENOENT') fail(`${label} source does not exist: ${reference.source}`);
    throw error;
  }
  const info = await stat(destination);
  return { sha256: await sha256(destination), size: info.size };
}

function verifyRanges(partitions) {
  const ranges = partitions
    .map(item => ({ name: item.name, start: parseOffset(item.offset), end: parseOffset(item.offset) + item.size }))
    .sort((a, b) => a.start - b.start);
  for (let index = 1; index < ranges.length; index += 1) {
    if (ranges[index].start < ranges[index - 1].end) {
      fail(`Flash ranges overlap: ${ranges[index - 1].name} and ${ranges[index].name}`);
    }
  }
}

async function prepare(root, tag) {
  const config = await loadConfig(root);
  const version = normalizeTag(tag);
  const staging = join(root, STAGING_DIRECTORY);
  await rm(staging, { recursive: true, force: true });
  await mkdir(staging, { recursive: true });

  const stagedAssets = new Map();
  async function stage(reference, label) {
    if (!stagedAssets.has(reference.asset)) {
      stagedAssets.set(reference.asset, await stageFile(root, staging, reference, label));
    }
    return stagedAssets.get(reference.asset);
  }
  const manifests = [];
  for (const target of config.targets) {
    const icon = await stage(target.icon, `${target.app.id} icon`);
    const partitions = [];
    for (const partition of target.partitions) {
      const staged = await stage(partition, `${target.app.id} partition ${partition.name}`);
      partitions.push({ ...partition, ...staged });
    }
    verifyRanges(partitions);
    const manifest = {
      schemaVersion: 1,
      app: {
        id: target.app.id,
        name: target.app.name,
        description: target.app.description,
        supportedDevices: target.app.supportedDevices,
        tags: target.app.tags || [],
        icon: { asset: target.icon.asset, sha256: icon.sha256 }
      },
      release: {
        version,
        partitions: partitions.map(({ name, asset, offset, sha256: digest }) => ({
          name,
          asset,
          offset,
          sha256: digest
        }))
      }
    };
    if (target.app.instructions) manifest.app.instructions = target.app.instructions;
    if (target.app.repository) manifest.app.repository = target.app.repository;
    await writeFile(join(staging, target.manifestAsset), `${JSON.stringify(manifest, null, 2)}\n`);
    manifests.push({ target, manifest });
  }

  // Re-read every final staged file so validation covers exactly what CI uploads.
  for (const { manifest } of manifests) {
    for (const reference of [manifest.app.icon, ...manifest.release.partitions]) {
      const path = join(staging, reference.asset);
      if (await sha256(path) !== reference.sha256) fail(`Final checksum mismatch: ${reference.asset}`);
    }
  }
  console.log(`Prepared ${relative(root, staging)} for ${manifests.map(item => item.target.app.id).join(', ')} ${version}`);
  for (const filename of [...config.targets.map(item => item.manifestAsset), ...stagedAssets.keys()]) {
    console.log(`- ${filename}`);
  }
}

function argument(name) {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const root = resolve(argument('--root') || process.cwd());
  const tag = argument('--tag') || process.env.RELEASE_TAG;
  prepare(root, tag).catch(error => {
    console.error(`Error: ${error.message}`);
    process.exitCode = 1;
  });
}

export { normalizeTag, parseOffset, prepare, validateAssetName, verifyRanges };
