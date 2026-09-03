/**
 * Package signing and verification.
 *
 * The exact scheme is documented in SIGNING.md next to this source, because
 * three independent implementations have to agree on it byte for byte: this
 * CLI, the `solweard` daemon that verifies at install time, and the store app
 * that verifies before it downloads. If you change anything here, change that
 * document and both verifiers in the same breath.
 *
 * In one paragraph: hash every file in the archive except signature.json with
 * SHA-256, write one line "<hex>  <path>\n" per file sorted by path, prefix the
 * whole thing with a domain separator line, and sign those bytes with Ed25519.
 */

import { createHash, createPrivateKey, createPublicKey, generateKeyPairSync, sign, verify } from "node:crypto";
import type { KeyObject } from "node:crypto";
import type { ZipEntry } from "./zip.js";

export const SIGNATURE_FILE = "signature.json";
export const SIGNATURE_VERSION = 1;

/** Domain separator, so a signature over these bytes cannot be replayed elsewhere. */
export const SIGNING_PREAMBLE = "SolWear .swa signature v1\n";

export interface SignatureDocument {
  version: number;
  algorithm: "ed25519";
  /** Base64 of the raw 32-byte Ed25519 public key. */
  publicKey: string;
  /** Base64 of the raw 64-byte signature. */
  signature: string;
  /** Path to lowercase hex SHA-256, for every file except signature.json. */
  files: Record<string, string>;
  signedAt: string;
}

export function sha256Hex(data: Buffer): string {
  return createHash("sha256").update(data).digest("hex");
}

/** Hash every entry that takes part in the signature, i.e. all but signature.json. */
export function digestEntries(entries: ZipEntry[]): Record<string, string> {
  const files: Record<string, string> = {};
  for (const entry of entries) {
    if (entry.path === SIGNATURE_FILE) continue;
    files[entry.path] = sha256Hex(entry.data);
  }
  return files;
}

/** The exact bytes that get signed. Sorted by path with a plain byte comparison. */
export function signingPayload(files: Record<string, string>): Buffer {
  const lines = Object.keys(files)
    .sort()
    .map((path) => `${files[path]}  ${path}\n`)
    .join("");
  return Buffer.from(SIGNING_PREAMBLE + lines, "utf8");
}

// Ed25519 keys travel as raw 32-byte values, which is what ed25519-dalek in the
// daemon expects. Node's crypto wants DER, so wrap and unwrap with these fixed
// prefixes rather than pulling in an ASN.1 library.
const SPKI_PREFIX = Buffer.from("302a300506032b6570032100", "hex");
const PKCS8_PREFIX = Buffer.from("302e020100300506032b657004220420", "hex");

export function rawToPublicKey(raw: Buffer): KeyObject {
  if (raw.length !== 32) throw new Error(`an Ed25519 public key is 32 bytes, got ${raw.length}`);
  return createPublicKey({ key: Buffer.concat([SPKI_PREFIX, raw]), format: "der", type: "spki" });
}

export function rawToPrivateKey(seed: Buffer): KeyObject {
  if (seed.length !== 32) throw new Error(`an Ed25519 private key seed is 32 bytes, got ${seed.length}`);
  return createPrivateKey({ key: Buffer.concat([PKCS8_PREFIX, seed]), format: "der", type: "pkcs8" });
}

export function publicKeyToRaw(key: KeyObject): Buffer {
  const der = key.export({ format: "der", type: "spki" });
  return Buffer.from(der.subarray(der.length - 32));
}

export function privateKeyToRaw(key: KeyObject): Buffer {
  const der = key.export({ format: "der", type: "pkcs8" });
  return Buffer.from(der.subarray(der.length - 32));
}

export interface SolwearKeypair {
  privateKey: KeyObject;
  publicKey: KeyObject;
  /** Base64 of the raw public key, as it appears in signature.json. */
  publicKeyBase64: string;
}

export function generateKeypair(): SolwearKeypair {
  const { privateKey, publicKey } = generateKeyPairSync("ed25519");
  return { privateKey, publicKey, publicKeyBase64: publicKeyToRaw(publicKey).toString("base64") };
}

/**
 * Read a signing key from disk contents. Three formats are accepted, because
 * developers arrive with whichever one their other tools produced:
 *
 *  - a SolWear key file, `{ "algorithm": "ed25519", "privateKey": "<base64 seed>" }`
 *  - a PEM PKCS#8 private key, as produced by openssl
 *  - a Solana CLI keypair, a JSON array of 64 bytes (32 seed + 32 public)
 */
export function parsePrivateKey(contents: Buffer): SolwearKeypair {
  const text = contents.toString("utf8").trim();

  if (text.startsWith("-----BEGIN")) {
    const privateKey = createPrivateKey({ key: text, format: "pem" });
    if (privateKey.asymmetricKeyType !== "ed25519") {
      throw new Error(`this PEM key is ${privateKey.asymmetricKeyType ?? "of an unknown type"}, not ed25519`);
    }
    const publicKey = createPublicKey(privateKey);
    return { privateKey, publicKey, publicKeyBase64: publicKeyToRaw(publicKey).toString("base64") };
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    throw new Error("the key file is neither PEM nor JSON");
  }

  let seed: Buffer;
  if (Array.isArray(parsed)) {
    if (parsed.length !== 64 && parsed.length !== 32) {
      throw new Error(`a JSON array keypair must hold 32 or 64 bytes, this one holds ${parsed.length}`);
    }
    seed = Buffer.from(parsed.slice(0, 32) as number[]);
  } else if (parsed && typeof parsed === "object" && "privateKey" in parsed) {
    const value = (parsed as { privateKey: unknown }).privateKey;
    if (typeof value !== "string") throw new Error('"privateKey" must be a base64 string');
    seed = Buffer.from(value, "base64");
    if (seed.length === 64) seed = seed.subarray(0, 32); // an expanded keypair
  } else {
    throw new Error('the JSON key file needs a "privateKey" field holding a base64 seed');
  }

  const privateKey = rawToPrivateKey(seed);
  const publicKey = createPublicKey(privateKey);
  return { privateKey, publicKey, publicKeyBase64: publicKeyToRaw(publicKey).toString("base64") };
}

/** Produce the signature document for a set of archive entries. */
export function signEntries(entries: ZipEntry[], keypair: SolwearKeypair): SignatureDocument {
  const files = digestEntries(entries);
  const payload = signingPayload(files);
  const signature = sign(null, payload, keypair.privateKey);
  return {
    version: SIGNATURE_VERSION,
    algorithm: "ed25519",
    publicKey: keypair.publicKeyBase64,
    signature: signature.toString("base64"),
    files,
    signedAt: new Date().toISOString(),
  };
}

export type VerifyResult =
  | { ok: true; publicKey: string; fileCount: number }
  | { ok: false; reason: string };

/**
 * Verify a signed archive. Every failure mode is reported as a reason string
 * rather than an exception, because callers want to show the wearer why an
 * install was refused.
 */
export function verifyEntries(entries: ZipEntry[], expectedPublicKey?: string): VerifyResult {
  const signatureEntry = entries.find((entry) => entry.path === SIGNATURE_FILE);
  if (!signatureEntry) return { ok: false, reason: "the package carries no signature.json" };

  let document: SignatureDocument;
  try {
    document = JSON.parse(signatureEntry.data.toString("utf8")) as SignatureDocument;
  } catch {
    return { ok: false, reason: "signature.json is not valid JSON" };
  }

  if (document.algorithm !== "ed25519") {
    return { ok: false, reason: `unsupported signature algorithm "${document.algorithm}"` };
  }
  if (document.version !== SIGNATURE_VERSION) {
    return { ok: false, reason: `signature format version ${document.version} is not supported` };
  }
  if (typeof document.publicKey !== "string" || typeof document.signature !== "string") {
    return { ok: false, reason: "signature.json is missing publicKey or signature" };
  }
  if (expectedPublicKey && expectedPublicKey !== document.publicKey) {
    return {
      ok: false,
      reason: "the package is signed by a different key than the registry lists for this publisher",
    };
  }

  // The file list in the document must describe exactly the archive contents.
  // Checking both directions is what stops a file being added or removed.
  const actual = digestEntries(entries);
  const declared = document.files ?? {};
  for (const path of Object.keys(actual)) {
    if (!(path in declared)) return { ok: false, reason: `"${path}" was added after the package was signed` };
    if (declared[path] !== actual[path]) return { ok: false, reason: `"${path}" was modified after signing` };
  }
  for (const path of Object.keys(declared)) {
    if (!(path in actual)) return { ok: false, reason: `"${path}" was removed after the package was signed` };
  }

  let publicKey;
  try {
    publicKey = rawToPublicKey(Buffer.from(document.publicKey, "base64"));
  } catch (error) {
    return { ok: false, reason: `the public key in signature.json is unusable: ${(error as Error).message}` };
  }

  const payload = signingPayload(declared);
  const valid = verify(null, payload, publicKey, Buffer.from(document.signature, "base64"));
  if (!valid) return { ok: false, reason: "the Ed25519 signature does not match the package contents" };

  return { ok: true, publicKey: document.publicKey, fileCount: Object.keys(actual).length };
}
