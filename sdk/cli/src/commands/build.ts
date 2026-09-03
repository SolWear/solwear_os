/** `solwear build` — bundle the app's TypeScript and stage everything into dist/. */

import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import * as esbuild from "esbuild";
import type { ParsedArgs } from "../args.js";
import { boolFlag, rejectUnknownFlags } from "../args.js";
import { CliError, colour, step, success } from "../log.js";
import { findProject, resolveSdk, type Project } from "../paths.js";

export const BUILD_FLAGS = ["watch", "minify", "sourcemap", "dir"];

export interface BuildResult {
  project: Project;
  distDir: string;
  bytes: number;
}

/** Entry points are looked for in this order; the first one present wins. */
const ENTRY_CANDIDATES = ["src/main.ts", "src/index.ts", "src/main.tsx", "src/app.ts", "src/main.js"];

export async function buildProject(
  project: Project,
  options: { minify?: boolean; sourcemap?: boolean; quiet?: boolean } = {},
): Promise<BuildResult> {
  const entry = ENTRY_CANDIDATES.map((candidate) => join(project.root, candidate)).find((path) =>
    existsSync(path),
  );
  if (!entry) {
    throw new CliError(`No entry point found in ${project.root}.`, {
      hint: `Create src/main.ts. The build looks for: ${ENTRY_CANDIDATES.join(", ")}.`,
    });
  }

  const htmlSource = join(project.root, project.manifest.entry);
  if (!existsSync(htmlSource)) {
    throw new CliError(`manifest.json points "entry" at ${project.manifest.entry}, which does not exist.`, {
      hint: `Create ${project.manifest.entry}, or change "entry" in manifest.json.`,
    });
  }

  const sdk = resolveSdk(project);
  if (!options.quiet) step(`bundling ${relative(project.root, entry)} with ${sdk.description}`);

  // dist/ is rebuilt from scratch so a renamed source file cannot leave a stale
  // copy behind that would then end up inside a signed package.
  rmSync(project.distDir, { recursive: true, force: true });
  mkdirSync(project.distDir, { recursive: true });

  const result = await esbuild.build({
    entryPoints: [entry],
    outfile: join(project.distDir, "app.js"),
    bundle: true,
    format: "esm",
    target: "es2022",
    platform: "browser",
    minify: options.minify ?? false,
    sourcemap: options.sourcemap ?? true,
    logLevel: "silent",
    alias: sdk.alias ? { "@solwear/sdk": sdk.alias } : undefined,
    define: {
      "process.env.NODE_ENV": JSON.stringify(options.minify ? "production" : "development"),
    },
  });

  if (result.warnings.length > 0 && !options.quiet) {
    for (const message of await esbuild.formatMessages(result.warnings, { kind: "warning", color: true })) {
      process.stderr.write(message);
    }
  }

  // Stage the static half of the package next to the bundle.
  copyInto(htmlSource, join(project.distDir, project.manifest.entry));
  copyInto(project.manifestPath, join(project.distDir, "manifest.json"));
  for (const extra of ["styles.css", "app.css"]) {
    const path = join(project.root, extra);
    if (existsSync(path)) copyInto(path, join(project.distDir, extra));
  }
  if (project.assetsDir) {
    cpSync(project.assetsDir, join(project.distDir, "assets"), { recursive: true });
  }
  if (project.manifest.icon && !existsSync(join(project.distDir, project.manifest.icon))) {
    throw new CliError(`manifest.json points "icon" at ${project.manifest.icon}, which is not in the build.`, {
      hint: `Put the file at ${join(project.root, project.manifest.icon)}, or remove "icon" from manifest.json.`,
    });
  }

  const bytes = readFileSync(join(project.distDir, "app.js")).length;
  if (!options.quiet) {
    success(`built ${colour.bold(project.manifest.id)} ${project.manifest.version} into dist/ (${formatBytes(bytes)})`);
  }
  return { project, distDir: project.distDir, bytes };
}

function copyInto(from: string, to: string): void {
  mkdirSync(join(to, ".."), { recursive: true });
  writeFileSync(to, readFileSync(from));
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} kB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

export async function buildCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, BUILD_FLAGS);
  const project = findProject(args.positionals[0] ?? process.cwd());
  const options = { minify: boolFlag(args, "minify"), sourcemap: !boolFlag(args, "no-sourcemap") };

  if (!boolFlag(args, "watch")) {
    await buildProject(project, options);
    return;
  }

  await buildProject(project, options);
  step("watching for changes, press Ctrl+C to stop");
  const { watch } = await import("node:fs");
  let rebuilding = false;
  const rebuild = async () => {
    if (rebuilding) return;
    rebuilding = true;
    setTimeout(async () => {
      try {
        await buildProject(project, options);
      } catch (error) {
        process.stderr.write(`${(error as Error).message}\n`);
      }
      rebuilding = false;
    }, 60);
  };
  // Watching the project root as well picks up index.html and manifest.json,
  // but dist/ has to be ignored or every rebuild would trigger the next one.
  for (const dir of [project.srcDir, project.root]) {
    if (!existsSync(dir)) continue;
    watch(dir, { recursive: dir === project.srcDir }, (_event, filename) => {
      const name = String(filename ?? "");
      if (name.startsWith("dist") || name.startsWith("node_modules") || name.startsWith(".")) return;
      void rebuild();
    });
  }
  await new Promise(() => {});
}
