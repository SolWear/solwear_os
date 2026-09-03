/** `solwear sign` — add signature.json to a .swa, and `solwear keygen` to make a key. */

import { chmodSync, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, join, relative } from "node:path";
import type { ParsedArgs } from "../args.js";
import { boolFlag, rejectUnknownFlags, stringFlag } from "../args.js";
import { CliError, colour, info, step, success, warn } from "../log.js";
import { packageFileName } from "../manifest.js";
import { findProject } from "../paths.js";
import {
  SIGNATURE_FILE,
  generateKeypair,
  parsePrivateKey,
  privateKeyToRaw,
  sha256Hex,
  signEntries,
  verifyEntries,
  type SolwearKeypair,
} from "../signing.js";
import { createZip, readZip } from "../zip.js";

export const SIGN_FLAGS = ["key", "package", "out"];
export const KEYGEN_FLAGS = ["out", "force"];

/** Where a developer key lives unless they say otherwise. */
export const DEFAULT_KEY_PATH = join(homedir(), ".solwear", "publisher.key.json");

export function loadKeypair(path: string): SolwearKeypair {
  if (!existsSync(path)) {
    throw new CliError(`No signing key at ${path}.`, {
      hint: `Create one with: solwear keygen --out ${path}`,
    });
  }
  try {
    return parsePrivateKey(readFileSync(path));
  } catch (error) {
    throw new CliError(`${path} is not a usable Ed25519 signing key: ${(error as Error).message}`, {
      hint:
        "Accepted formats are a SolWear key file, a PEM PKCS#8 Ed25519 key, " +
        "or a Solana CLI keypair (a JSON array of 64 numbers).",
    });
  }
}

/** Locate the package to sign: an explicit path, or the one this project builds. */
function resolvePackagePath(args: ParsedArgs): string {
  const explicit = stringFlag(args, "package") ?? args.positionals[0];
  if (explicit) {
    if (!existsSync(explicit)) throw new CliError(`No such package: ${explicit}`);
    return explicit;
  }
  const project = findProject();
  const candidate = join(project.distDir, packageFileName(project.manifest));
  if (!existsSync(candidate)) {
    throw new CliError(`No package to sign at ${relative(project.root, candidate)}.`, {
      hint: "Run `solwear package` first, or pass --package <file.swa>.",
    });
  }
  return candidate;
}

export function signPackageFile(packagePath: string, keypair: SolwearKeypair, outPath?: string): string {
  const entries = readZip(readFileSync(packagePath));
  const withoutSignature = entries.filter((entry) => entry.path !== SIGNATURE_FILE);
  const document = signEntries(withoutSignature, keypair);

  const signed = [
    ...withoutSignature,
    { path: SIGNATURE_FILE, data: Buffer.from(`${JSON.stringify(document, null, 2)}\n`, "utf8") },
  ];
  const archive = createZip(signed);
  const target = outPath ?? packagePath;
  writeFileSync(target, archive);

  // Verify what was just written rather than what was held in memory, so a bug
  // in the writer cannot ship a package that no device will accept.
  const check = verifyEntries(readZip(readFileSync(target)));
  if (!check.ok) {
    throw new CliError(`The package was signed but does not verify: ${check.reason}`, {
      hint: "This is a bug in solwear. Please report it with the package that triggered it.",
    });
  }
  return target;
}

export async function signCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, SIGN_FLAGS);
  const keyPath = stringFlag(args, "key", { default: DEFAULT_KEY_PATH })!;
  const packagePath = resolvePackagePath(args);
  const keypair = loadKeypair(keyPath);

  step(`signing ${packagePath}`);
  const target = signPackageFile(packagePath, keypair, stringFlag(args, "out"));
  const bytes = readFileSync(target);

  success(`signed with ${colour.bold(keypair.publicKeyBase64)}`);
  info(`  ${target}`);
  info(`  sha256 ${sha256Hex(bytes)}`);
}

export async function keygenCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, KEYGEN_FLAGS);
  const out = stringFlag(args, "out", { default: DEFAULT_KEY_PATH })!;
  if (existsSync(out) && !boolFlag(args, "force")) {
    throw new CliError(`${out} already exists.`, {
      hint: "Pass --force to overwrite it, but remember that anything signed with the old key stays signed with the old key.",
    });
  }

  const keypair = generateKeypair();
  const document = {
    algorithm: "ed25519",
    createdAt: new Date().toISOString(),
    publicKey: keypair.publicKeyBase64,
    privateKey: privateKeyToRaw(keypair.privateKey).toString("base64"),
  };

  mkdirSync(dirname(out), { recursive: true });
  writeFileSync(out, `${JSON.stringify(document, null, 2)}\n`, { mode: 0o600 });
  chmodSync(out, 0o600);

  success(`wrote a new publisher key to ${out}`);
  info(`  public key: ${colour.bold(keypair.publicKeyBase64)}`);
  warn("This file is the only copy of your private key. Back it up, and never commit it.");
}
