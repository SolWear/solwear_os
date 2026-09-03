#!/usr/bin/env node
/**
 * Download and cryptographically verify every package in a registry index.
 *
 * This intentionally uses only Node built-ins. Registry CI is part of the
 * trust boundary and should not acquire a dependency tree merely to inspect a
 * ZIP file or verify an Ed25519 signature.
 */

import { createHash, createPublicKey, verify } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { inflateRawSync } from "node:zlib";
import { validateManifestDocument } from "./validate.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const SIGNATURE_FILE = "signature.json";
const SIGNING_PREAMBLE = "SolWear .swa signature v1\n";
const SPKI_PREFIX = Buffer.from("302a300506032b6570032100", "hex");
const SHA256 = /^[0-9a-f]{64}$/;
const BASE64_KEY = /^[A-Za-z0-9+/]{43}=$/;
const BASE64_SIGNATURE = /^[A-Za-z0-9+/]{86}==$/;
const MAX_PACKAGE_BYTES = 64 * 1024 * 1024;
const MAX_ENTRIES = 4096;

export class PackageVerificationError extends Error {
  constructor(message) {
    super(message);
    this.name = "PackageVerificationError";
  }
}

function fail(message) {
  throw new PackageVerificationError(message);
}

function sha256(data) {
  return createHash("sha256").update(data).digest("hex");
}

function canonicalBase64(value, pattern, byteLength, label) {
  if (typeof value !== "string" || !pattern.test(value)) {
    fail(`${label} is not canonical base64`);
  }
  const decoded = Buffer.from(value, "base64");
  if (decoded.length !== byteLength || decoded.toString("base64") !== value) {
    fail(`${label} must encode exactly ${byteLength} raw bytes`);
  }
  return decoded;
}

function safeArchivePath(name) {
  if (!/^[\x21-\x7e]+$/.test(name)) fail(`archive path ${JSON.stringify(name)} is not portable ASCII`);
  if (name.startsWith("/") || name.includes("\\")) fail(`unsafe archive path ${JSON.stringify(name)}`);
  const segments = name.split("/");
  if (segments.some((part) => part === "" || part === "." || part === "..")) {
    fail(`unsafe archive path ${JSON.stringify(name)}`);
  }
}

/** Read regular files from a non-ZIP64 .swa archive. */
export function readZipEntries(input) {
  const bytes = Buffer.from(input);
  if (bytes.length < 22) fail("not a ZIP archive: end-of-central-directory record is missing");

  const firstPossibleEocd = Math.max(0, bytes.length - 65_557);
  let eocd = -1;
  for (let offset = bytes.length - 22; offset >= firstPossibleEocd; offset--) {
    if (bytes.readUInt32LE(offset) === 0x06054b50) {
      eocd = offset;
      break;
    }
  }
  if (eocd === -1) fail("not a ZIP archive: end-of-central-directory record is missing");

  const disk = bytes.readUInt16LE(eocd + 4);
  const centralDisk = bytes.readUInt16LE(eocd + 6);
  const entriesOnDisk = bytes.readUInt16LE(eocd + 8);
  const entryCount = bytes.readUInt16LE(eocd + 10);
  const centralSize = bytes.readUInt32LE(eocd + 12);
  const centralOffset = bytes.readUInt32LE(eocd + 16);
  if (disk !== 0 || centralDisk !== 0 || entriesOnDisk !== entryCount) {
    fail("multi-disk ZIP archives are not supported");
  }
  if (entryCount === 0xffff || centralSize === 0xffffffff || centralOffset === 0xffffffff) {
    fail("ZIP64 packages are not supported");
  }
  if (entryCount > MAX_ENTRIES) fail(`archive contains ${entryCount} entries; limit is ${MAX_ENTRIES}`);
  if (centralOffset + centralSize > eocd) fail("ZIP central directory points outside the archive");

  const files = new Map();
  let totalUncompressed = 0;
  let cursor = centralOffset;

  for (let i = 0; i < entryCount; i++) {
    if (cursor + 46 > bytes.length || bytes.readUInt32LE(cursor) !== 0x02014b50) {
      fail(`ZIP central directory entry ${i} is malformed`);
    }
    const flags = bytes.readUInt16LE(cursor + 8);
    const method = bytes.readUInt16LE(cursor + 10);
    const compressedSize = bytes.readUInt32LE(cursor + 20);
    const uncompressedSize = bytes.readUInt32LE(cursor + 24);
    const nameLength = bytes.readUInt16LE(cursor + 28);
    const extraLength = bytes.readUInt16LE(cursor + 30);
    const commentLength = bytes.readUInt16LE(cursor + 32);
    const localOffset = bytes.readUInt32LE(cursor + 42);
    const next = cursor + 46 + nameLength + extraLength + commentLength;
    if (next > bytes.length) fail(`ZIP central directory entry ${i} is truncated`);
    if ((flags & 1) !== 0) fail("encrypted ZIP entries are not supported");
    if (compressedSize === 0xffffffff || uncompressedSize === 0xffffffff || localOffset === 0xffffffff) {
      fail("ZIP64 entries are not supported");
    }

    const name = bytes.subarray(cursor + 46, cursor + 46 + nameLength).toString("utf8");
    cursor = next;
    if (name.endsWith("/")) continue;
    safeArchivePath(name);
    if (files.has(name)) fail(`archive contains duplicate entry ${JSON.stringify(name)}`);

    if (localOffset + 30 > bytes.length || bytes.readUInt32LE(localOffset) !== 0x04034b50) {
      fail(`local ZIP header for ${JSON.stringify(name)} is malformed`);
    }
    const localMethod = bytes.readUInt16LE(localOffset + 8);
    const localNameLength = bytes.readUInt16LE(localOffset + 26);
    const localExtraLength = bytes.readUInt16LE(localOffset + 28);
    const localName = bytes
      .subarray(localOffset + 30, localOffset + 30 + localNameLength)
      .toString("utf8");
    if (localName !== name || localMethod !== method) {
      fail(`central and local ZIP headers disagree for ${JSON.stringify(name)}`);
    }
    const dataStart = localOffset + 30 + localNameLength + localExtraLength;
    const dataEnd = dataStart + compressedSize;
    if (dataEnd > bytes.length) fail(`compressed data for ${JSON.stringify(name)} is truncated`);
    const compressed = bytes.subarray(dataStart, dataEnd);

    let data;
    if (method === 0) data = Buffer.from(compressed);
    else if (method === 8) {
      try {
        data = inflateRawSync(compressed, { maxOutputLength: MAX_PACKAGE_BYTES + 1 });
      } catch (error) {
        fail(`cannot inflate ${JSON.stringify(name)}: ${error.message}`);
      }
    } else {
      fail(`unsupported ZIP compression method ${method} for ${JSON.stringify(name)}`);
    }
    if (data.length !== uncompressedSize) fail(`uncompressed size mismatch for ${JSON.stringify(name)}`);
    totalUncompressed += data.length;
    if (totalUncompressed > MAX_PACKAGE_BYTES) fail("archive expands beyond the 64 MiB package limit");
    files.set(name, data);
  }

  if (cursor !== centralOffset + centralSize) fail("ZIP central directory size does not match its entries");
  return files;
}

function signaturePayload(hashes) {
  const listing = Object.keys(hashes)
    .sort()
    .map((path) => `${hashes[path]}  ${path}\n`)
    .join("");
  return Buffer.from(SIGNING_PREAMBLE + listing, "utf8");
}

function parseJsonFile(files, name) {
  const data = files.get(name);
  if (!data) fail(`package is missing required file ${JSON.stringify(name)}`);
  try {
    return JSON.parse(data.toString("utf8"));
  } catch (error) {
    fail(`${name} is not valid JSON: ${error.message}`);
  }
}

/** Verify package contents after ZIP parsing. Exported for focused unit tests. */
export function verifyPackageEntries(files, registryEntry) {
  if (!(files instanceof Map)) fail("internal error: package entries must be a Map");
  if (!files.has("manifest.json")) fail('package is missing required file "manifest.json"');
  if (!files.has("index.html")) fail('package is missing required file "index.html"');
  if (!files.has(SIGNATURE_FILE)) fail('package is missing required file "signature.json"');

  const manifest = parseJsonFile(files, "manifest.json");
  const manifestProblems = validateManifestDocument(manifest);
  if (manifestProblems.length > 0) {
    const first = manifestProblems[0];
    fail(`manifest${first.path || "/"} ${first.message}`);
  }
  if (!files.has(manifest.entry)) fail(`manifest entry ${JSON.stringify(manifest.entry)} is missing`);
  if (manifest.icon && !files.has(manifest.icon)) {
    fail(`manifest icon ${JSON.stringify(manifest.icon)} is missing`);
  }

  const matches = [
    ["id", registryEntry.id, manifest.id],
    ["name", registryEntry.name, manifest.name],
    ["version", registryEntry.version, manifest.version],
    ["type", registryEntry.type, manifest.type],
    ["sdk", registryEntry.sdk, manifest.sdk],
    ["description", registryEntry.description, manifest.description],
    ["publisher", registryEntry.publisher, manifest.author],
  ];
  for (const [field, registryValue, manifestValue] of matches) {
    if (registryValue !== manifestValue) {
      fail(`${field} differs: registry has ${JSON.stringify(registryValue)}, manifest has ${JSON.stringify(manifestValue)}`);
    }
  }

  const document = parseJsonFile(files, SIGNATURE_FILE);
  if (document.version !== 1) fail(`unsupported signature format version ${JSON.stringify(document.version)}`);
  if (document.algorithm !== "ed25519") fail(`unsupported signature algorithm ${JSON.stringify(document.algorithm)}`);
  const publicKey = canonicalBase64(document.publicKey, BASE64_KEY, 32, "signature publicKey");
  const signature = canonicalBase64(document.signature, BASE64_SIGNATURE, 64, "signature");
  if (document.publicKey !== registryEntry.publisherKey) {
    fail("signature publicKey differs from registry publisherKey");
  }
  if (!document.files || typeof document.files !== "object" || Array.isArray(document.files)) {
    fail("signature files must be an object");
  }

  const actualHashes = {};
  for (const [path, data] of files) {
    if (path !== SIGNATURE_FILE) actualHashes[path] = sha256(data);
  }
  const actualPaths = Object.keys(actualHashes).sort();
  const declaredPaths = Object.keys(document.files).sort();
  if (actualPaths.length !== declaredPaths.length) fail("signature file list does not cover the archive exactly");
  for (let i = 0; i < actualPaths.length; i++) {
    const path = actualPaths[i];
    if (declaredPaths[i] !== path) fail("signature file list does not cover the archive exactly");
    const declared = document.files[path];
    if (typeof declared !== "string" || !SHA256.test(declared)) {
      fail(`signature digest for ${JSON.stringify(path)} is not lowercase SHA-256 hex`);
    }
    if (declared !== actualHashes[path]) fail(`${JSON.stringify(path)} was modified after signing`);
  }

  let key;
  try {
    key = createPublicKey({ key: Buffer.concat([SPKI_PREFIX, publicKey]), format: "der", type: "spki" });
  } catch (error) {
    fail(`signature publicKey is unusable: ${error.message}`);
  }
  if (!verify(null, signaturePayload(actualHashes), key, signature)) {
    fail("Ed25519 signature does not match the package contents");
  }
  return { manifest, fileCount: actualPaths.length };
}

export function verifyPackageBytes(input, registryEntry) {
  const bytes = Buffer.from(input);
  if (bytes.length > MAX_PACKAGE_BYTES) fail("package exceeds the 64 MiB download limit");
  const digest = sha256(bytes);
  if (digest !== registryEntry.sha256) {
    fail(`package SHA-256 is ${digest}, registry declares ${registryEntry.sha256}`);
  }
  return verifyPackageEntries(readZipEntries(bytes), registryEntry);
}

async function download(url, timeoutMs) {
  let response;
  try {
    response = await fetch(url, { redirect: "follow", signal: AbortSignal.timeout(timeoutMs) });
  } catch (error) {
    fail(`download failed: ${error.message}`);
  }
  if (!response.ok) fail(`download failed: HTTP ${response.status} ${response.statusText}`);
  const advertised = Number(response.headers.get("content-length"));
  if (Number.isFinite(advertised) && advertised > MAX_PACKAGE_BYTES) {
    fail(`download advertises ${advertised} bytes; limit is ${MAX_PACKAGE_BYTES}`);
  }
  if (!response.body) fail("download returned no response body");

  const chunks = [];
  let total = 0;
  for await (const chunk of response.body) {
    total += chunk.length;
    if (total > MAX_PACKAGE_BYTES) fail("download exceeds the 64 MiB package limit");
    chunks.push(chunk);
  }
  return Buffer.concat(chunks, total);
}

/**
 * Where an entry's package can be read without a network.
 *
 * A registry `url` always ends in `/<id>-<version>.swa`, so the file name is
 * derived from the entry itself rather than parsed out of the URL: a hostile
 * index cannot point the local lookup at some other file that way.
 */
export function localPackagePath(packagesDir, entry) {
  return join(packagesDir, `${entry.id}-${entry.version}.swa`);
}

/**
 * Fetch an entry's bytes, preferring the copy held next to the index.
 *
 * Both halves matter. A developer, and CI on a pull request, need to verify
 * exactly the bytes in the tree before anything is hosted; once a package is
 * hosted, the same command has to check what devices will actually receive.
 * The mode is reported per entry so a local pass is never mistaken for proof
 * that the hosted file is correct.
 */
async function loadPackage(entry, options) {
  const local = options.packagesDir ? localPackagePath(options.packagesDir, entry) : null;
  if (local && existsSync(local)) {
    const bytes = readFileSync(local);
    return { bytes, origin: `local ${relative(process.cwd(), local) || local}` };
  }
  if (options.offline) {
    fail(
      local
        ? `no local package at ${relative(process.cwd(), local) || local}, and --offline forbids downloading it`
        : "--offline was given but no --packages-dir was",
    );
  }
  return { bytes: await download(entry.url, options.timeoutMs), origin: `hosted ${entry.url}` };
}

function parseArgs(argv) {
  const options = {
    index: resolve(HERE, "index.json"),
    packagesDir: resolve(HERE, "packages"),
    offline: false,
    timeoutMs: 120_000,
  };
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--index") options.index = resolve(argv[++i]);
    else if (argv[i] === "--packages-dir") options.packagesDir = resolve(argv[++i]);
    else if (argv[i] === "--no-packages-dir") options.packagesDir = null;
    else if (argv[i] === "--offline") options.offline = true;
    else if (argv[i] === "--timeout-ms") options.timeoutMs = Number(argv[++i]);
    else if (argv[i] === "--help" || argv[i] === "-h") options.help = true;
    else fail(`unknown argument ${JSON.stringify(argv[i])}`);
  }
  if (!Number.isSafeInteger(options.timeoutMs) || options.timeoutMs <= 0) fail("--timeout-ms must be a positive integer");
  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    console.log(
      [
        "Usage: node store/registry/verify-packages.mjs [options]",
        "",
        "  --index <path>          registry index to verify (default: store/registry/index.json)",
        "  --packages-dir <path>   look for <id>-<version>.swa here before downloading",
        "                          (default: store/registry/packages)",
        "  --no-packages-dir       always download, even when a local copy exists",
        "  --offline               never download; every entry must resolve locally",
        "  --timeout-ms <n>        download timeout (default: 120000)",
      ].join("\n"),
    );
    return;
  }
  const index = JSON.parse(readFileSync(options.index, "utf8"));
  const apps = Array.isArray(index.apps) ? index.apps : [];
  let failures = 0;
  for (const entry of apps) {
    const label = `${entry.id} ${entry.version}`;
    try {
      const { bytes, origin } = await loadPackage(entry, options);
      const result = verifyPackageBytes(bytes, entry);
      console.log(`  ok    ${label} (${result.fileCount} signed files, ${origin})`);
    } catch (error) {
      failures++;
      console.error(`  FAIL  ${label}: ${error.message}`);
    }
  }
  if (failures > 0) fail(`${failures}/${apps.length} packages failed verification`);
  console.log(`Verified ${apps.length} package${apps.length === 1 ? "" : "s"}`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error.message);
    process.exit(1);
  });
}
