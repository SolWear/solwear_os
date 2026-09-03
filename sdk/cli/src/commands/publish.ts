/** `solwear publish` — prepare a registry entry for a signed package. */

import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { basename, join, relative } from "node:path";
import type { ParsedArgs } from "../args.js";
import { boolFlag, rejectUnknownFlags, stringFlag } from "../args.js";
import { CliError, colour, info, step, success, warn } from "../log.js";
import { packageFileName, validateManifest, type Manifest } from "../manifest.js";
import { findMonorepoRoot, findProject } from "../paths.js";
import { sha256Hex, verifyEntries } from "../signing.js";
import { readZip } from "../zip.js";
import { packageProject } from "./package.js";

export const PUBLISH_FLAGS = ["package", "url", "registry", "publisher", "no-build", "dry-run"];

export interface RegistryEntry {
  id: string;
  name: string;
  version: string;
  sdk: string;
  type: "app" | "watchface";
  url: string;
  sha256: string;
  publisher: string;
  publisherKey: string;
  description?: string;
}

interface RegistryIndex {
  schemaVersion: number;
  apps: RegistryEntry[];
}

/** Where a published package is expected to live, unless --url says otherwise. */
function defaultUrl(manifest: Manifest): string {
  return `https://packages.solwear.tech/${manifest.id}/${packageFileName(manifest)}`;
}

export async function publishCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, PUBLISH_FLAGS);

  const project = findProject();
  let packagePath = stringFlag(args, "package");
  if (!packagePath) {
    const candidate = join(project.distDir, packageFileName(project.manifest));
    if (boolFlag(args, "no-build") && existsSync(candidate)) packagePath = candidate;
    else packagePath = (await packageProject(project, { build: !boolFlag(args, "no-build") })).path;
  }
  if (!existsSync(packagePath)) throw new CliError(`No such package: ${packagePath}`);

  const bytes = readFileSync(packagePath);
  const entries = readZip(bytes);

  const manifestEntry = entries.find((entry) => entry.path === "manifest.json");
  if (!manifestEntry) throw new CliError(`${basename(packagePath)} contains no manifest.json.`);
  const manifest = validateManifest(JSON.parse(manifestEntry.data.toString("utf8")), "manifest.json");

  // The registry only accepts signed packages, so refuse early and clearly.
  const verification = verifyEntries(entries);
  if (!verification.ok) {
    throw new CliError(`This package cannot be published: ${verification.reason}.`, {
      hint: "Sign it first: solwear sign --key ~/.solwear/publisher.key.json",
    });
  }

  const entry: RegistryEntry = {
    id: manifest.id,
    name: manifest.name,
    version: manifest.version,
    sdk: manifest.sdk,
    type: manifest.type,
    url: stringFlag(args, "url") ?? defaultUrl(manifest),
    sha256: sha256Hex(bytes),
    publisher: stringFlag(args, "publisher") ?? manifest.author ?? "Unknown",
    publisherKey: verification.publicKey,
  };
  if (manifest.description) entry.description = manifest.description;

  const entryPath = join(project.distDir, "registry-entry.json");
  writeFileSync(entryPath, `${JSON.stringify(entry, null, 2)}\n`);
  step(`wrote ${relative(project.root, entryPath)}`);

  const registryPath = stringFlag(args, "registry") ?? defaultRegistryPath();
  if (boolFlag(args, "dry-run") || !registryPath) {
    printManualInstructions(entry, registryPath);
    return;
  }

  mergeIntoRegistry(registryPath, entry, boolFlag(args, "dry-run"));
  success(`added ${colour.bold(`${entry.id} ${entry.version}`)} to ${registryPath}`);
  info("");
  info("  Publishing is a pull request. Commit the registry change and open one:");
  info("    git add store/registry/index.json");
  info(`    git commit -m "publish ${entry.id} ${entry.version}"`);
  info("    git push && gh pr create");
  info("");
  info(`  Upload the package itself to ${entry.url} before the PR is merged,`);
  info("  or CI will not be able to check its hash.");
}

function defaultRegistryPath(): string | undefined {
  const root = findMonorepoRoot();
  if (!root) return undefined;
  const path = join(root, "store", "registry", "index.json");
  return existsSync(path) ? path : undefined;
}

function printManualInstructions(entry: RegistryEntry, registryPath: string | undefined): void {
  info("");
  info(registryPath ? "  Dry run, nothing was written to the registry." : "  No local registry checkout found.");
  info("  Add this entry to store/registry/index.json and open a pull request:");
  info("");
  for (const line of JSON.stringify(entry, null, 2).split("\n")) info(`    ${line}`);
  info("");
  info(`  Upload the package to ${entry.url} first, so CI can verify its hash.`);
}

/**
 * Append a version to the registry's history. Publishing the same id/version
 * twice is refused; published artifacts are immutable even when the bytes are
 * identical, and older versions remain available for audit and rollback.
 */
export function mergeIntoRegistry(registryPath: string, entry: RegistryEntry, dryRun: boolean): void {
  let index: RegistryIndex;
  try {
    index = JSON.parse(readFileSync(registryPath, "utf8")) as RegistryIndex;
  } catch (error) {
    throw new CliError(`${registryPath} is not readable as JSON: ${(error as Error).message}`);
  }
  if (!Array.isArray(index.apps)) {
    throw new CliError(`${registryPath} has no "apps" array.`);
  }

  const duplicate = index.apps.find((app) => app.id === entry.id && app.version === entry.version);
  if (duplicate) {
    throw new CliError(`${entry.id} ${entry.version} is already published.`, {
      hint: "Bump the version in manifest.json. A published version is immutable.",
    });
  }
  const newest = index.apps
    .filter((app) => app.id === entry.id)
    .sort((a, b) => compareVersions(b.version, a.version))[0];
  if (newest && compareVersions(entry.version, newest.version) < 0) {
    warn(`the registry already lists ${entry.id} ${newest.version}, which is newer than ${entry.version}`);
  }
  index.apps.push(entry);

  index.schemaVersion ??= 1;
  index.apps.sort((a, b) => {
    if (a.id !== b.id) return a.id < b.id ? -1 : 1;
    return compareVersions(a.version, b.version);
  });
  if (!dryRun) writeFileSync(registryPath, `${JSON.stringify(index, null, 2)}\n`);
}

/** Compare two semantic versions, ignoring any prerelease suffix. */
export function compareVersions(a: string, b: string): number {
  const parse = (value: string) => value.split(/[-+]/)[0]!.split(".").map((part) => Number(part) || 0);
  const left = parse(a);
  const right = parse(b);
  for (let i = 0; i < 3; i++) {
    const diff = (left[i] ?? 0) - (right[i] ?? 0);
    if (diff !== 0) return diff < 0 ? -1 : 1;
  }
  return 0;
}
