#!/usr/bin/env node
/**
 * The SolWear host emulator.
 *
 * Serves the real shell and the real app bundle against a mock HAL, in a window
 * that draws the device bezel. Usually started through `solwear run`.
 *
 *   node bin/solwear-emulator.mjs --app ../../apps/watchface/dist --profile pi-round-480
 */

import { existsSync, readFileSync, readdirSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { EmulatorServer } from "../src/server.mjs";
import { findBrowser, openWindow } from "../src/launch.mjs";

const here = dirname(fileURLToPath(import.meta.url));
const hostDir = resolve(here, "..");
const monorepo = resolve(hostDir, "..", "..");
const profilesDir = join(hostDir, "profiles");

const started = Date.now();

function parseArgs(argv) {
  const flags = {};
  for (let i = 0; i < argv.length; i++) {
    const token = argv[i];
    if (!token.startsWith("--")) continue;
    const body = token.slice(2);
    const eq = body.indexOf("=");
    if (eq >= 0) {
      flags[body.slice(0, eq)] = body.slice(eq + 1);
    } else if (argv[i + 1] && !argv[i + 1].startsWith("--")) {
      flags[body] = argv[++i];
    } else {
      flags[body] = true;
    }
  }
  return flags;
}

function die(message, hint) {
  process.stderr.write(`\nx ${message}\n`);
  if (hint) process.stderr.write(`\n  Try this: ${hint}\n\n`);
  process.exit(1);
}

function listProfiles() {
  return readdirSync(profilesDir)
    .filter((name) => name.endsWith(".json"))
    .map((name) => JSON.parse(readFileSync(join(profilesDir, name), "utf8")));
}

function loadProfile(name) {
  const path = join(profilesDir, `${name}.json`);
  if (!existsSync(path)) {
    const available = listProfiles().map((profile) => profile.id);
    die(`"${name}" is not a device profile.`, `Pick one of: ${available.join(", ")}`);
  }
  const profile = JSON.parse(readFileSync(path, "utf8"));
  if (!profile.screen?.width || !profile.screen?.height) {
    die(`${path} has no screen dimensions.`, "Every profile needs screen.width, screen.height and screen.shape.");
  }
  return profile;
}

/**
 * Prefer the real shell built by the OS team; fall back to the reference shell
 * that ships with the emulator. Both are ordinary web content served from
 * /shell/, so an app cannot tell the difference.
 */
function resolveShell(flags) {
  if (flags.shell) {
    const custom = resolve(flags.shell);
    if (!existsSync(join(custom, "index.html"))) die(`No index.html in ${custom}.`);
    return { dir: custom, source: `custom (${custom})` };
  }
  const built = join(monorepo, "os", "shell", "dist");
  if (existsSync(join(built, "index.html"))) return { dir: built, source: "os/shell/dist" };
  return { dir: join(hostDir, "web", "shell"), source: "emulator reference shell" };
}

const flags = parseArgs(process.argv.slice(2));

if (flags.help === true || flags.h === true) {
  process.stdout.write(
    [
      "",
      "  solwear-emulator — the fast PC simulator",
      "",
      "  --app <dir>        built app directory to load (default: the watchface demo)",
      "  --profile <name>   device profile (default: pi-round-480)",
      "  --port <number>    HTTP port (default: 8731, matching the device)",
      "  --rpc-port <n>     JSON-RPC WebSocket port (default: 8730)",
      "  --shell <dir>      serve a specific shell build",
      "  --mock <file.json> scripted HAL values",
      "  --no-window        start the server only and print the URL",
      "  --list-profiles    print the device profiles and exit",
      "",
    ].join("\n") + "\n",
  );
  process.exit(0);
}

if (flags["list-profiles"]) {
  for (const profile of listProfiles()) {
    const { width, height, shape } = profile.screen;
    process.stdout.write(`${profile.id.padEnd(20)} ${width}x${height} ${shape.padEnd(7)} ${profile.label ?? ""}\n`);
  }
  process.exit(0);
}

const profile = loadProfile(flags.profile ?? "pi-round-480");

// Which app to load. Defaulting to the bundled watchface means `solwear-emulator`
// with no arguments still shows something real.
const appDir = resolve(flags.app ?? join(monorepo, "apps", "watchface", "dist"));
const manifestPath = join(appDir, "manifest.json");
if (!existsSync(manifestPath)) {
  die(
    `No built app at ${appDir}.`,
    appDir.includes("apps/watchface")
      ? "Build the demo first: (cd apps/watchface && node ../../sdk/cli/dist/bin.js build)"
      : "Run `solwear build` in the app directory first.",
  );
}
const appManifest = JSON.parse(readFileSync(manifestPath, "utf8"));
appManifest.url = `/apps/${appManifest.id}/${appManifest.entry ?? "index.html"}`;

// Other installed apps come from the monorepo's apps/ directory, so the
// launcher and the store have something to list.
const systemApps = [];
const appRoots = { [appManifest.id]: appDir };
const appsRoot = join(monorepo, "apps");
if (existsSync(appsRoot)) {
  for (const name of readdirSync(appsRoot)) {
    const projectDir = join(appsRoot, name);
    const candidate = join(projectDir, "manifest.json");
    if (!existsSync(candidate)) continue;
    try {
      const manifest = JSON.parse(readFileSync(candidate, "utf8"));
      if (manifest.id !== appManifest.id) {
        const builtDir = join(projectDir, "dist");
        manifest.url = `/apps/${manifest.id}/${manifest.entry ?? "index.html"}`;
        systemApps.push(manifest);
        appRoots[manifest.id] = existsSync(join(builtDir, manifest.entry ?? "index.html")) ? builtDir : projectDir;
      }
    } catch {
      // A manifest that does not parse is the app author's problem, and it
      // must not stop the emulator from starting.
    }
  }
}

const shell = resolveShell(flags);
const mock = flags.mock ? JSON.parse(readFileSync(resolve(flags.mock), "utf8")) : undefined;

const server = new EmulatorServer({
  profile,
  profiles: listProfiles().map((entry) => ({ id: entry.id, label: entry.label, screen: entry.screen })),
  allProfiles: listProfiles(),
  webDir: join(hostDir, "web"),
  shellDir: shell.dir,
  shellSource: shell.source,
  appDir,
  appManifest,
  systemApps,
  appRoots,
  httpPort: Number(flags.port ?? 8731),
  rpcPort: Number(flags["rpc-port"] ?? 8730),
  mock,
});

try {
  await server.listen();
} catch (error) {
  die(error.message);
}
server.watchApp();

const bezel = profile.window ?? { width: profile.screen.width + 120, height: profile.screen.height + 160 };

process.stdout.write(
  [
    "",
    `  SolWear emulator   ${profile.label ?? profile.id}  ${profile.screen.width}x${profile.screen.height} ${profile.screen.shape}`,
    `  app                ${appManifest.name} ${appManifest.version} (${appManifest.id})`,
    `  shell              ${shell.source}`,
    `  http               ${server.url}`,
    `  json-rpc           ws://127.0.0.1:${server.options.rpcPort}/`,
    "",
  ].join("\n") + "\n",
);

if (flags["no-window"]) {
  process.stdout.write(`  Open ${server.url} in a browser. Press Ctrl+C to stop.\n\n`);
} else {
  const { mode } = openWindow(server.url, { width: bezel.width + 400, height: Math.max(bezel.height, 760) });
  if (mode === "fallback") {
    process.stdout.write(
      "  ! No Chromium-based browser found, so the emulator opened your default browser instead.\n" +
        "    The device window will be inside a normal tab. Install Chrome or Chromium for the\n" +
        "    proper chrome-less window: brew install --cask google-chrome\n\n",
    );
  }
  process.stdout.write(`  ready in ${Date.now() - started} ms. Press Ctrl+C to stop.\n\n`);
}

const stop = () => {
  server.close();
  process.exit(0);
};
process.on("SIGINT", stop);
process.on("SIGTERM", stop);
