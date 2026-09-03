/** `solwear package` — turn dist/ into a .swa archive. */

import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { join, relative, sep } from "node:path";
import type { ParsedArgs } from "../args.js";
import { boolFlag, rejectUnknownFlags, stringFlag } from "../args.js";
import { CliError, colour, info, step, success } from "../log.js";
import { packageFileName, validateManifest, type Manifest } from "../manifest.js";
import { findProject, type Project } from "../paths.js";
import { createZip, type ZipEntry } from "../zip.js";
import { sha256Hex } from "../signing.js";
import { buildProject, formatBytes } from "./build.js";

export const PACKAGE_FLAGS = ["out", "no-build", "minify"];

/**
 * Collect the files that belong in the archive.
 *
 * Packages are written into dist/ as the spec requires, so any .swa already
 * sitting there is skipped: a package must never contain a previous package.
 * Source maps are skipped too; they are a development aid and would roughly
 * double the size of what gets shipped to a watch.
 */
export function collectPackageEntries(distDir: string): ZipEntry[] {
  const entries: ZipEntry[] = [];

  const walk = (dir: string): void => {
    for (const name of readdirSync(dir).sort()) {
      const full = join(dir, name);
      const stats = statSync(full);
      if (stats.isDirectory()) {
        walk(full);
        continue;
      }
      if (name.endsWith(".swa") || name.endsWith(".map")) continue;
      if (name === ".DS_Store") continue;
      entries.push({
        path: relative(distDir, full).split(sep).join("/"),
        data: readFileSync(full),
      });
    }
  };

  walk(distDir);
  return entries;
}

/** Check that a set of entries really is a loadable package. */
export function validatePackageEntries(entries: ZipEntry[]): Manifest {
  const manifestEntry = entries.find((entry) => entry.path === "manifest.json");
  if (!manifestEntry) {
    throw new CliError("The package has no manifest.json at its root.", {
      hint: "Run `solwear build` first; it copies manifest.json into dist/.",
    });
  }
  const manifest = validateManifest(JSON.parse(manifestEntry.data.toString("utf8")), "manifest.json");

  if (!entries.some((entry) => entry.path === manifest.entry)) {
    throw new CliError(`The package is missing its entry point "${manifest.entry}".`, {
      hint: 'Check the "entry" field in manifest.json and rerun `solwear build`.',
    });
  }
  if (manifest.icon && !entries.some((entry) => entry.path === manifest.icon)) {
    throw new CliError(`The package is missing its icon "${manifest.icon}".`);
  }
  return manifest;
}

export interface PackageResult {
  path: string;
  sha256: string;
  bytes: number;
  manifest: Manifest;
}

export async function packageProject(
  project: Project,
  options: { build?: boolean; out?: string; minify?: boolean; quiet?: boolean } = {},
): Promise<PackageResult> {
  if (options.build !== false) {
    await buildProject(project, { minify: options.minify ?? false, quiet: options.quiet ?? false });
  } else if (!existsSync(project.distDir)) {
    throw new CliError("dist/ does not exist and --no-build was given.", {
      hint: "Drop --no-build, or run `solwear build` first.",
    });
  }

  const entries = collectPackageEntries(project.distDir);
  const manifest = validatePackageEntries(entries);
  const archive = createZip(entries);

  const outPath = options.out ?? join(project.distDir, packageFileName(manifest));
  writeFileSync(outPath, archive);
  const sha256 = sha256Hex(archive);

  if (!options.quiet) {
    success(`packaged ${colour.bold(relative(project.root, outPath))} (${formatBytes(archive.length)})`);
    info(`  ${entries.length} files, sha256 ${sha256}`);
    info(`  ${colour.dim("unsigned: run `solwear sign --key <path>` before publishing")}`);
  }

  return { path: outPath, sha256, bytes: archive.length, manifest };
}

export async function packageCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, PACKAGE_FLAGS);
  const project = findProject(args.positionals[0] ?? process.cwd());
  step(`packaging ${project.manifest.id}`);
  const options: Parameters<typeof packageProject>[1] = {
    build: !boolFlag(args, "no-build"),
    minify: boolFlag(args, "minify"),
  };
  const out = stringFlag(args, "out");
  if (out) options.out = out;
  await packageProject(project, options);
}
