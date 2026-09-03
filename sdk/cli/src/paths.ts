/** Locating the current project, the monorepo, and the SDK the CLI should link against. */

import { existsSync, readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { CliError } from "./log.js";
import { readManifest, type Manifest } from "./manifest.js";

/** The installed root of this CLI package (the directory holding package.json). */
export const cliRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");

export const templatesDir = join(cliRoot, "templates");

/**
 * Walk up from a directory looking for the SolWear monorepo. Both a checkout
 * and an npm-installed CLI are supported: in a checkout the emulator and the
 * shell live next to us, and when installed from the registry they do not, so
 * every caller has to cope with `undefined`.
 */
export function findMonorepoRoot(from: string = process.cwd()): string | undefined {
  const candidates = [from, cliRoot];
  for (const start of candidates) {
    let current = resolve(start);
    for (;;) {
      if (existsSync(join(current, "docs", "ARCHITECTURE.md")) && existsSync(join(current, "emulator"))) {
        return current;
      }
      const parent = dirname(current);
      if (parent === current) break;
      current = parent;
    }
  }
  return undefined;
}

export interface Project {
  root: string;
  manifestPath: string;
  manifest: Manifest;
  srcDir: string;
  distDir: string;
  assetsDir: string | undefined;
}

/** Find the app project that contains `from`, by walking up to a manifest.json. */
export function findProject(from: string = process.cwd()): Project {
  let current = resolve(from);
  for (;;) {
    const manifestPath = join(current, "manifest.json");
    if (existsSync(manifestPath)) {
      const manifest = readManifest(manifestPath);
      const assetsDir = join(current, "assets");
      return {
        root: current,
        manifestPath,
        manifest,
        srcDir: join(current, "src"),
        distDir: join(current, "dist"),
        assetsDir: existsSync(assetsDir) ? assetsDir : undefined,
      };
    }
    const parent = dirname(current);
    if (parent === current) {
      throw new CliError(`No SolWear app found in ${resolve(from)} or any parent directory.`, {
        hint: "cd into your app, or create one with `solwear new my-app`.",
      });
    }
    current = parent;
  }
}

/**
 * Resolve the on-disk location of `@solwear/sdk`.
 *
 * A project that ran `npm install` has it in node_modules and nothing special
 * is needed. A freshly generated project inside the monorepo has not installed
 * anything, so the build aliases the import straight at sdk/runtime. This is
 * what lets `solwear new` produce something that builds with no further steps.
 */
export function resolveSdk(project: Project): { alias: string | undefined; description: string } {
  const local = join(project.root, "node_modules", "@solwear", "sdk");
  if (existsSync(join(local, "package.json"))) {
    return { alias: undefined, description: "node_modules/@solwear/sdk" };
  }

  const monorepo = findMonorepoRoot(project.root);
  if (monorepo) {
    const runtime = join(monorepo, "sdk", "runtime");
    const entry = join(runtime, "dist", "index.js");
    if (existsSync(entry)) return { alias: entry, description: "sdk/runtime/dist (monorepo)" };
    if (existsSync(join(runtime, "package.json"))) {
      throw new CliError("The workspace copy of @solwear/sdk has not been built yet.", {
        hint: `Run: (cd ${runtime} && npm install && npm run build)`,
      });
    }
  }

  throw new CliError("Cannot find @solwear/sdk.", {
    hint: `Run: npm install @solwear/sdk   (inside ${project.root})`,
  });
}

export function readJsonFile<T>(path: string): T {
  return JSON.parse(readFileSync(path, "utf8")) as T;
}
