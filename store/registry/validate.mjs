#!/usr/bin/env node
/**
 * SolWear registry validator.
 *
 * Checks store/registry/index.json — and optionally an app manifest — against
 * the JSON Schemas in store/registry/schema, plus the semantic rules that a
 * schema cannot express:
 *
 *   - every id and version pair is unique
 *   - entries for one id are contiguous and strictly ascending in semver order
 *   - every entry carries a well-formed SHA-256 digest
 *   - every entry carries well-formed signature fields, and the publisher and
 *     publisher key stay constant across versions of an id
 *   - the package URL is HTTPS and named <id>-<version>.swa
 *
 * Exits 0 when everything passes and 1 when anything fails. This is what CI
 * runs on every publishing pull request.
 *
 * Usage:
 *   node store/registry/validate.mjs
 *   node store/registry/validate.mjs --index path/to/index.json
 *   node store/registry/validate.mjs --manifest path/to/manifest.json
 *   node store/registry/validate.mjs --index a.json --manifest b.json
 *   node store/registry/validate.mjs --quiet
 */

import { readFileSync, existsSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { validate } from "./lib/jsonschema.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const registrySchema = JSON.parse(
  readFileSync(join(HERE, "schema", "registry.schema.json"), "utf8"),
);
const manifestSchema = JSON.parse(
  readFileSync(join(HERE, "schema", "manifest.schema.json"), "utf8"),
);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

const SHA256 = /^[0-9a-f]{64}$/;
const BASE64_KEY = /^[A-Za-z0-9+/]{43}=$/;
const SEMVER = /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/;

function isCanonicalPublicKey(value) {
  if (typeof value !== "string" || !BASE64_KEY.test(value)) return false;
  const decoded = Buffer.from(value, "base64");
  return decoded.length === 32 && decoded.toString("base64") === value;
}

/** Compare two semantic versions. Returns <0, 0 or >0. */
function compareSemver(a, b) {
  const pa = a.split(".").map(Number);
  const pb = b.split(".").map(Number);
  for (let i = 0; i < 3; i++) {
    if (pa[i] !== pb[i]) return pa[i] - pb[i];
  }
  return 0;
}

function readJson(path, problems) {
  try {
    return JSON.parse(readFileSync(path, "utf8"));
  } catch (err) {
    problems.push({ path: "/", message: `is not valid JSON: ${err.message}` });
    return null;
  }
}

/* ------------------------------------------------------------------ */
/* Semantic rules the schema cannot express                            */
/* ------------------------------------------------------------------ */

function checkRegistrySemantics(registry, problems) {
  const apps = Array.isArray(registry?.apps) ? registry.apps : [];

  const seenPairs = new Map(); // "id@version" -> index
  const highestForId = new Map(); // id -> { version, index }
  const identityForId = new Map(); // id -> { publisher, publisherKey, type, name, index }
  const idFirstSeenAt = new Map();
  const idLastSeenAt = new Map();

  apps.forEach((entry, i) => {
    const at = (field) => `/apps/${i}${field ? `/${field}` : ""}`;
    const { id, version, sha256, publisherKey, publisher, type, name, url } = entry || {};

    if (typeof id !== "string" || typeof version !== "string") return; // schema already reported it

    // --- id and version uniqueness ---------------------------------
    const pair = `${id}@${version}`;
    if (seenPairs.has(pair)) {
      problems.push({
        path: at("version"),
        message: `duplicate entry: ${pair} is already published at /apps/${seenPairs.get(pair)}`,
      });
    } else {
      seenPairs.set(pair, i);
    }

    // --- semver ordering -------------------------------------------
    if (SEMVER.test(version)) {
      const highest = highestForId.get(id);
      if (highest) {
        const cmp = compareSemver(version, highest.version);
        if (cmp < 0) {
          problems.push({
            path: at("version"),
            message: `${version} is not greater than ${highest.version}, already published for ${id} at /apps/${highest.index}; versions for one id must ascend`,
          });
        } else if (cmp > 0) {
          highestForId.set(id, { version, index: i });
        }
      } else {
        highestForId.set(id, { version, index: i });
      }
    }

    // --- entries for one id must stay contiguous --------------------
    if (!idFirstSeenAt.has(id)) idFirstSeenAt.set(id, i);
    const last = idLastSeenAt.get(id);
    if (last !== undefined && last !== i - 1) {
      problems.push({
        path: at(),
        message: `entries for ${id} are not contiguous; the previous one is at /apps/${last}`,
      });
    }
    idLastSeenAt.set(id, i);

    // --- SHA-256 presence and shape ---------------------------------
    if (sha256 === undefined || sha256 === null || sha256 === "") {
      problems.push({ path: at("sha256"), message: "is required and must not be empty" });
    } else if (typeof sha256 !== "string" || !SHA256.test(sha256)) {
      problems.push({
        path: at("sha256"),
        message: "must be 64 lowercase hexadecimal characters",
      });
    } else if (/^0{64}$/.test(sha256)) {
      problems.push({ path: at("sha256"), message: "is a placeholder digest, not a real one" });
    }

    // --- signature field shape --------------------------------------
    if (publisherKey === undefined || publisherKey === null || publisherKey === "") {
      problems.push({
        path: at("publisherKey"),
        message: "is required: a package with no publisher key cannot be verified",
      });
    } else if (!isCanonicalPublicKey(publisherKey)) {
      problems.push({
        path: at("publisherKey"),
        message: "must be canonical base64 for a raw 32-byte Ed25519 public key",
      });
    }

    // --- publisher identity must be stable across versions ----------
    const known = identityForId.get(id);
    if (known) {
      if (publisherKey && known.publisherKey && publisherKey !== known.publisherKey) {
        problems.push({
          path: at("publisherKey"),
          message: `differs from the key used for ${id} at /apps/${known.index}; a key change needs review before it can be merged`,
        });
      }
      if (publisher && known.publisher && publisher !== known.publisher) {
        problems.push({
          path: at("publisher"),
          message: `differs from the publisher of ${id} at /apps/${known.index}`,
        });
      }
      if (type && known.type && type !== known.type) {
        problems.push({
          path: at("type"),
          message: `differs from the type of ${id} at /apps/${known.index}; an id may not change type`,
        });
      }
      if (name && known.name && name !== known.name) {
        problems.push({
          path: at("name"),
          message: `differs from the name of ${id} at /apps/${known.index}`,
        });
      }
    } else {
      identityForId.set(id, { publisher, publisherKey, type, name, index: i });
    }

    // --- package URL ------------------------------------------------
    if (typeof url === "string" && SEMVER.test(version)) {
      const expected = `${id}-${version}.swa`;
      if (!url.endsWith(`/${expected}`)) {
        problems.push({
          path: at("url"),
          message: `must be an HTTPS URL ending in /${expected}`,
        });
      }
    }
  });
}

export function checkManifestSemantics(manifest, problems) {
  if (!manifest || typeof manifest !== "object") return;
  const { entry, icon, capabilities, type } = manifest;

  for (const [field, value] of Object.entries({ entry, icon })) {
    if (typeof value === "string" && (value.startsWith("/") || value.includes(".."))) {
      problems.push({
        path: `/${field}`,
        message: "must be a path inside the archive; it may not be absolute or contain ..",
      });
    }
  }

  if (type === "watchface" && Array.isArray(capabilities) && capabilities.includes("wallet")) {
    problems.push({
      path: "/capabilities",
      message:
        "a watchface may not request the wallet capability; move signing into an app of type app",
    });
  }
}

/** Cross-check a manifest against the registry entry that claims to describe it. */
export function crossCheck(manifest, registry, problems) {
  const apps = Array.isArray(registry?.apps) ? registry.apps : [];
  const i = apps.findIndex((e) => e?.id === manifest?.id && e?.version === manifest?.version);
  if (i === -1) return; // Not published yet, which is normal before the pull request lands.

  const entry = apps[i];
  const pairs = [
    ["name", entry.name, manifest.name],
    ["type", entry.type, manifest.type],
    ["sdk", entry.sdk, manifest.sdk],
    ["description", entry.description, manifest.description],
    ["publisher", entry.publisher, manifest.author],
  ];
  for (const [field, inRegistry, inManifest] of pairs) {
    if (inRegistry !== undefined && inManifest !== undefined && inRegistry !== inManifest) {
      problems.push({
        path: `/apps/${i}/${field}`,
        message: `is ${JSON.stringify(inRegistry)} but the manifest says ${JSON.stringify(inManifest)}`,
      });
    }
  }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

function parseArgs(argv) {
  const opts = { index: join(HERE, "index.json"), manifest: null, quiet: false };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--index") opts.index = resolve(argv[++i]);
    else if (arg === "--manifest") opts.manifest = resolve(argv[++i]);
    else if (arg === "--quiet" || arg === "-q") opts.quiet = true;
    else if (arg === "--help" || arg === "-h") opts.help = true;
    else {
      console.error(`Unknown argument: ${arg}`);
      process.exit(2);
    }
  }
  return opts;
}

function report(label, problems, quiet) {
  if (problems.length === 0) return 0;
  const width = Math.min(40, Math.max(...problems.map((p) => p.path.length)) + 2);
  console.error(label);
  for (const p of problems) console.error(`  ${p.path.padEnd(width)} ${p.message}`);
  if (!quiet) console.error("");
  return problems.length;
}

export function validateManifestDocument(manifest) {
  const problems = [];
  validate(manifest, manifestSchema, manifestSchema, "", problems);
  checkManifestSemantics(manifest, problems);
  return problems;
}

function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) {
    console.log(
      [
        "Usage: node store/registry/validate.mjs [options]",
        "",
        "  --index <path>     registry index to validate (default: store/registry/index.json)",
        "  --manifest <path>  additionally validate an app manifest",
        "  --quiet            print only failures",
        "  --help             show this message",
      ].join("\n"),
    );
    return 0;
  }

  let total = 0;
  let registry = null;

  if (!existsSync(opts.index)) {
    console.error(`Registry index not found: ${opts.index}`);
    return 1;
  }

  const registryProblems = [];
  registry = readJson(opts.index, registryProblems);
  if (registry) {
    validate(registry, registrySchema, registrySchema, "", registryProblems);
    checkRegistrySemantics(registry, registryProblems);
  }
  total += report(relative(process.cwd(), opts.index) || opts.index, registryProblems, opts.quiet);

  if (opts.manifest) {
    if (!existsSync(opts.manifest)) {
      console.error(`Manifest not found: ${opts.manifest}`);
      return 1;
    }
    const manifestProblems = [];
    const manifest = readJson(opts.manifest, manifestProblems);
    if (manifest) {
      manifestProblems.push(...validateManifestDocument(manifest));
      if (registry) crossCheck(manifest, registry, manifestProblems);
    }
    total += report(
      relative(process.cwd(), opts.manifest) || opts.manifest,
      manifestProblems,
      opts.quiet,
    );
  }

  if (total > 0) {
    console.error(`${total} problem${total === 1 ? "" : "s"} found`);
    return 1;
  }

  if (!opts.quiet) {
    const count = registry?.apps?.length ?? 0;
    console.log(`Registry is valid: ${count} app${count === 1 ? "" : "s"}${opts.manifest ? ", manifest is valid" : ""}`);
  }
  return 0;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  process.exit(main());
}
