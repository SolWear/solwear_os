#!/usr/bin/env node
/**
 * Fixture tests for the registry validator.
 *
 * Every case runs validate.mjs as a subprocess and asserts both the exit code
 * and that the reported problem is the one we meant to provoke. A validator
 * that fails for the wrong reason is not doing its job, so matching the message
 * matters as much as matching the exit code.
 *
 * Run: node store/registry/test.mjs
 */

import { spawnSync } from "node:child_process";
import { createHash, generateKeyPairSync, sign } from "node:crypto";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { verifyPackageBytes } from "./verify-packages.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const VALIDATOR = join(HERE, "validate.mjs");
const VALID = join(HERE, "fixtures", "valid");
const INVALID = join(HERE, "fixtures", "invalid");

function run(args) {
  const r = spawnSync(process.execPath, [VALIDATOR, ...args], { encoding: "utf8" });
  return { code: r.status, out: `${r.stdout}${r.stderr}` };
}

const cases = [];
const pass = (name, args) => cases.push({ name, args, expect: 0 });
const reject = (name, args, expect) => cases.push({ name, args, expect: 1, contains: expect });

/* --- the real registry and the good fixtures must pass -------------- */

pass("the published registry is valid", []);
pass("the valid fixture index is valid", ["--index", join(VALID, "index.json")]);
pass("a valid manifest matching its registry entry", [
  "--index",
  join(VALID, "index.json"),
  "--manifest",
  join(VALID, "manifest.json"),
]);

/* --- schema conformance --------------------------------------------- */

reject("an unknown field is rejected", ["--index", join(INVALID, "unknown-field.json")], "is not an allowed property");
reject("an unsupported schemaVersion is rejected", ["--index", join(INVALID, "bad-schema-version.json")], "must be one of 1");
reject("a non reverse-DNS id is rejected", ["--index", join(INVALID, "bad-id.json")], "must match");
reject("a plain http package URL is rejected", ["--index", join(INVALID, "insecure-url.json")], "must match");

/* --- SHA-256 -------------------------------------------------------- */

reject(
  "a tampered digest is rejected",
  ["--index", join(INVALID, "tampered-sha256.json")],
  "must be 64 lowercase hexadecimal characters",
);
reject(
  "a placeholder digest is rejected",
  ["--index", join(INVALID, "placeholder-sha256.json")],
  "is a placeholder digest",
);

/* --- signature fields ----------------------------------------------- */

reject(
  "an entry with no publisher key is rejected",
  ["--index", join(INVALID, "missing-signature.json")],
  "is required",
);
reject(
  "a malformed publisher key is rejected",
  ["--index", join(INVALID, "malformed-publisher-key.json")],
  "canonical base64 for a raw 32-byte Ed25519 public key",
);
reject(
  "a silently rotated publisher key is rejected",
  ["--index", join(INVALID, "rotated-publisher-key.json")],
  "a key change needs review",
);

/* --- uniqueness and ordering ---------------------------------------- */

reject(
  "a duplicate id and version is rejected",
  ["--index", join(INVALID, "duplicate-id.json")],
  "duplicate entry",
);
reject(
  "a version that goes backwards is rejected",
  ["--index", join(INVALID, "version-regression.json")],
  "versions for one id must ascend",
);
reject(
  "entries for one id split apart are rejected",
  ["--index", join(INVALID, "non-contiguous-id.json")],
  "are not contiguous",
);
reject(
  "a URL that does not name the id and version is rejected",
  ["--index", join(INVALID, "url-mismatch.json")],
  "must be an HTTPS URL ending in",
);

/* --- manifests ------------------------------------------------------ */

reject(
  "a manifest that disagrees with its registry entry is rejected",
  ["--index", join(VALID, "index.json"), "--manifest", join(INVALID, "tampered-manifest.json")],
  "but the manifest says",
);
reject(
  "an unknown capability is rejected",
  ["--manifest", join(INVALID, "manifest-unknown-capability.json")],
  "must be one of",
);
reject(
  "a watchface requesting the wallet capability is rejected",
  ["--manifest", join(INVALID, "manifest-watchface-wallet.json")],
  "may not request the wallet capability",
);
reject(
  "an entry path escaping the archive is rejected",
  ["--manifest", join(INVALID, "manifest-path-escape.json")],
  "may not be absolute or contain",
);
reject(
  "a v-prefixed version is rejected",
  ["--manifest", join(INVALID, "manifest-bad-version.json")],
  "must match",
);

/* --- run ------------------------------------------------------------ */

let failures = 0;
for (const c of cases) {
  const { code, out } = run(c.args);
  const codeOk = code === c.expect;
  const textOk = !c.contains || out.includes(c.contains);
  if (codeOk && textOk) {
    console.log(`  ok    ${c.name}`);
  } else {
    failures++;
    console.log(`  FAIL  ${c.name}`);
    if (!codeOk) console.log(`          expected exit ${c.expect}, got ${code}`);
    if (!textOk) console.log(`          expected output to contain: ${c.contains}`);
    console.log(
      out
        .trimEnd()
        .split("\n")
        .map((l) => `          | ${l}`)
        .join("\n"),
    );
  }
}

console.log(`\n${cases.length - failures}/${cases.length} registry validator cases passed`);

/* --- complete hosted package verification -------------------------- */

const sha256 = (data) => createHash("sha256").update(data).digest("hex");

function crc32(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}

/** Minimal stored ZIP writer used only to exercise the independent reader. */
function zip(entries) {
  const locals = [];
  const centrals = [];
  let offset = 0;
  for (const [name, value] of entries) {
    const nameBytes = Buffer.from(name);
    const data = Buffer.from(value);
    const checksum = crc32(data);
    const local = Buffer.alloc(30 + nameBytes.length);
    local.writeUInt32LE(0x04034b50, 0);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(0x0800, 6);
    local.writeUInt16LE(0, 8);
    local.writeUInt32LE(checksum, 14);
    local.writeUInt32LE(data.length, 18);
    local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBytes.length, 26);
    nameBytes.copy(local, 30);
    locals.push(local, data);

    const central = Buffer.alloc(46 + nameBytes.length);
    central.writeUInt32LE(0x02014b50, 0);
    central.writeUInt16LE(20, 4);
    central.writeUInt16LE(20, 6);
    central.writeUInt16LE(0x0800, 8);
    central.writeUInt16LE(0, 10);
    central.writeUInt32LE(checksum, 16);
    central.writeUInt32LE(data.length, 20);
    central.writeUInt32LE(data.length, 24);
    central.writeUInt16LE(nameBytes.length, 28);
    central.writeUInt32LE(offset, 42);
    nameBytes.copy(central, 46);
    centrals.push(central);
    offset += local.length + data.length;
  }
  const centralSize = centrals.reduce((sum, part) => sum + part.length, 0);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(entries.size, 8);
  eocd.writeUInt16LE(entries.size, 10);
  eocd.writeUInt32LE(centralSize, 12);
  eocd.writeUInt32LE(offset, 16);
  return Buffer.concat([...locals, ...centrals, eocd]);
}

function signedFixture(mutateAfterSigning) {
  const manifest = {
    id: "tech.example.timer",
    name: "Interval Timer",
    version: "1.2.0",
    sdk: "0.1",
    type: "app",
    entry: "index.html",
    capabilities: ["system"],
    author: "Example Ltd",
    description: "Interval timer with haptic cues.",
  };
  const files = new Map([
    ["manifest.json", Buffer.from(JSON.stringify(manifest))],
    ["index.html", Buffer.from("<!doctype html><title>Timer</title>")],
    ["assets/app.js", Buffer.from("console.log('timer')")],
  ]);
  const hashes = Object.fromEntries([...files].map(([path, data]) => [path, sha256(data)]));
  const payload = Buffer.from(
    "SolWear .swa signature v1\n" +
      Object.keys(hashes)
        .sort()
        .map((path) => `${hashes[path]}  ${path}\n`)
        .join(""),
  );
  const { privateKey, publicKey } = generateKeyPairSync("ed25519");
  const publicKeyBase64 = Buffer.from(
    publicKey.export({ format: "der", type: "spki" }).subarray(-32),
  ).toString("base64");
  files.set(
    "signature.json",
    Buffer.from(
      JSON.stringify({
        version: 1,
        algorithm: "ed25519",
        publicKey: publicKeyBase64,
        signature: sign(null, payload, privateKey).toString("base64"),
        files: hashes,
        signedAt: "2026-01-01T00:00:00.000Z",
      }),
    ),
  );
  if (mutateAfterSigning) mutateAfterSigning(files);
  const archive = zip(files);
  const entry = {
    id: manifest.id,
    name: manifest.name,
    version: manifest.version,
    type: manifest.type,
    url: `https://apps.example.com/${manifest.id}-${manifest.version}.swa`,
    sha256: sha256(archive),
    publisher: manifest.author,
    publisherKey: publicKeyBase64,
    description: manifest.description,
    sdk: manifest.sdk,
  };
  return { archive, entry, files };
}

const packageCases = [
  {
    name: "a matching signed .swa is accepted",
    run() {
      const { archive, entry } = signedFixture();
      verifyPackageBytes(archive, entry);
    },
  },
  {
    name: "a registry SHA-256 mismatch is rejected",
    contains: "registry declares",
    run() {
      const { archive, entry } = signedFixture();
      verifyPackageBytes(archive, { ...entry, sha256: "1".repeat(64) });
    },
  },
  {
    name: "a file modified after signing is rejected",
    contains: "modified after signing",
    run() {
      const { archive, entry } = signedFixture((files) => files.set("index.html", Buffer.from("tampered")));
      verifyPackageBytes(archive, entry);
    },
  },
  {
    name: "a file added after signing is rejected",
    contains: "does not cover the archive exactly",
    run() {
      const { archive, entry } = signedFixture((files) => files.set("assets/extra.js", Buffer.from("evil")));
      verifyPackageBytes(archive, entry);
    },
  },
  {
    name: "a registry publisher key mismatch is rejected",
    contains: "differs from registry publisherKey",
    run() {
      const { archive, entry } = signedFixture();
      verifyPackageBytes(archive, { ...entry, publisherKey: Buffer.alloc(32, 7).toString("base64") });
    },
  },
];

let packageFailures = 0;
for (const c of packageCases) {
  try {
    c.run();
    if (c.contains) throw new Error(`expected rejection containing: ${c.contains}`);
    console.log(`  ok    ${c.name}`);
  } catch (error) {
    if (c.contains && error.message.includes(c.contains)) console.log(`  ok    ${c.name}`);
    else {
      packageFailures++;
      console.log(`  FAIL  ${c.name}`);
      console.log(`          | ${error.message}`);
    }
  }
}

console.log(`\n${packageCases.length - packageFailures}/${packageCases.length} package verification cases passed`);
failures += packageFailures;
process.exit(failures === 0 ? 0 : 1);
