/**
 * Produce browser bundles alongside the tsc output.
 *
 * dist/solwear-sdk.js        ESM, for apps bundled by the CLI or any bundler
 * dist/solwear-sdk.global.js IIFE that defines window.solwear, for a plain
 *                            <script> tag in an app that uses no build step
 */
import { build } from "esbuild";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const entry = resolve(root, "src/index.ts");

const common = { bundle: true, entryPoints: [entry], target: "es2022", platform: "browser", sourcemap: true };

await build({ ...common, format: "esm", outfile: resolve(root, "dist/solwear-sdk.js") });
await build({
  ...common,
  format: "iife",
  globalName: "SolWearSDK",
  outfile: resolve(root, "dist/solwear-sdk.global.js"),
  footer: { js: "window.solwear = SolWearSDK.solwear;" },
});

console.log("built dist/solwear-sdk.js and dist/solwear-sdk.global.js");
