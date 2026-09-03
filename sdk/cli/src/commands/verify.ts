/**
 * `solwear verify` — inspect a package and check its signature.
 *
 * This is the same check the daemon performs at install time and the store app
 * performs before download, so a package that verifies here will be accepted
 * there. Having it as a command means a failing install can be diagnosed
 * without a device.
 */

import { existsSync, readFileSync } from "node:fs";
import { basename } from "node:path";
import type { ParsedArgs } from "../args.js";
import { rejectUnknownFlags } from "../args.js";
import { CliError, colour, fail, info, success } from "../log.js";
import { validateManifest } from "../manifest.js";
import { sha256Hex, verifyEntries } from "../signing.js";
import { readZip, ZipFormatError } from "../zip.js";
import { formatBytes } from "./build.js";

export async function verifyCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, ["key"]);

  const path = args.positionals[0];
  if (!path) throw new CliError("`solwear verify` needs a package.", { hint: "solwear verify dist/*.swa" });
  if (!existsSync(path)) throw new CliError(`No such package: ${path}`);

  const bytes = readFileSync(path);
  let entries;
  try {
    entries = readZip(bytes);
  } catch (error) {
    throw new CliError(
      error instanceof ZipFormatError ? `${basename(path)} is not a valid .swa: ${error.message}` : String(error),
      { hint: "A .swa is a ZIP archive. Rebuild it with `solwear package`." },
    );
  }

  const manifestEntry = entries.find((entry) => entry.path === "manifest.json");
  if (!manifestEntry) throw new CliError(`${basename(path)} contains no manifest.json.`);
  const manifest = validateManifest(JSON.parse(manifestEntry.data.toString("utf8")), "manifest.json");

  info("");
  info(`  ${colour.bold(manifest.name)} ${manifest.version}  (${manifest.id})`);
  info(`  type          ${manifest.type}`);
  info(`  capabilities  ${manifest.capabilities.join(", ") || "none"}`);
  info(`  size          ${formatBytes(bytes.length)} in ${entries.length} files`);
  info(`  sha256        ${sha256Hex(bytes)}`);
  info("");
  for (const entry of entries) {
    info(`    ${entry.path.padEnd(28)} ${formatBytes(entry.data.length)}`);
  }
  info("");

  const key = typeof args.flags["key"] === "string" ? (args.flags["key"] as string) : undefined;
  const result = verifyEntries(entries, key);
  if (result.ok) {
    success(`signature valid, signed by ${result.publicKey}`);
    return;
  }

  fail(`signature check failed: ${result.reason}`);
  throw new CliError("This package would be refused by a device.", {
    hint: "Rebuild and sign it again: solwear package && solwear sign --key <path>",
  });
}
