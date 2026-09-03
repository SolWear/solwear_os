// Bundles the shell with esbuild into dist/. The output is plain ES2020 with
// no framework, which keeps the first paint fast on a Raspberry Pi 4.

import * as esbuild from "esbuild";
import { cp, mkdir, rm } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const outdir = resolve(here, "dist");
const watch = process.argv.includes("--watch");

await rm(outdir, { recursive: true, force: true });
await mkdir(outdir, { recursive: true });
await cp(resolve(here, "public"), outdir, { recursive: true });

/** @type {import("esbuild").BuildOptions} */
const options = {
  entryPoints: [resolve(here, "src/main.ts")],
  bundle: true,
  format: "esm",
  target: ["es2020", "chrome108"],
  platform: "browser",
  outdir,
  entryNames: "shell",
  assetNames: "assets/[name]-[hash]",
  minify: !watch,
  sourcemap: watch ? "inline" : false,
  legalComments: "none",
  logLevel: "info",
};

if (watch) {
  const context = await esbuild.context(options);
  await context.watch();
  console.log("shell: watching for changes");
} else {
  await esbuild.build(options);
  console.log(`shell: built into ${outdir}`);
}
