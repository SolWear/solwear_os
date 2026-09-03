/**
 * Start the emulator the way a developer does, and check what it actually
 * serves.
 *
 * The other test file exercises the daemon and the server as objects. This one
 * runs `bin/solwear-emulator.mjs` as a process, because the parts that break in
 * practice — argument parsing, profile loading, picking a shell, binding two
 * ports — only exist in the entry point. Ports are 0 so the test cannot collide
 * with an emulator or a `solweard` that is already running, and `--no-window`
 * keeps it headless.
 */

import assert from "node:assert/strict";
import test from "node:test";
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { setTimeout as delay } from "node:timers/promises";

const hostDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const binary = join(hostDir, "bin", "solwear-emulator.mjs");
const monorepo = resolve(hostDir, "..", "..");

/**
 * The emulator loads a built app, and defaults to the bundled watchface. A
 * clean checkout has not built it yet, and a startup test that fails for that
 * reason would be reporting on the wrong thing.
 */
const watchfaceBuilt = existsSync(join(monorepo, "apps", "watchface", "dist", "manifest.json"));
const skipReason = watchfaceBuilt
  ? undefined
  : "apps/watchface has not been built; run its build before the emulator startup tests";

/** Every profile that ships, and the screen it must report. */
const PROFILES = [
  { id: "pi-round-240", width: 240, height: 240, shape: "round" },
  { id: "pi-round-480", width: 480, height: 480, shape: "round" },
  { id: "pi-square-320", width: 320, height: 320, shape: "square" },
  { id: "pi-wide-800x480", width: 800, height: 480, shape: "rect" },
];

/** Start the emulator and wait until it prints both of its addresses. */
async function startEmulator(profileId) {
  const child = spawn(
    process.execPath,
    [binary, "--no-window", "--profile", profileId, "--port", "0", "--rpc-port", "0"],
    { stdio: ["ignore", "pipe", "pipe"] },
  );
  let output = "";
  child.stdout.on("data", (chunk) => (output += chunk));
  child.stderr.on("data", (chunk) => (output += chunk));

  const deadline = Date.now() + 20_000;
  while (Date.now() < deadline) {
    const http = output.match(/http\s+(http:\/\/127\.0\.0\.1:\d+)\//);
    const rpc = output.match(/json-rpc\s+(ws:\/\/127\.0\.0\.1:\d+)\//);
    if (http && rpc) return { child, output: () => output, http: http[1], rpc: rpc[1] };
    if (child.exitCode !== null) break;
    await delay(25);
  }
  child.kill("SIGKILL");
  throw new Error(`the emulator did not start with profile ${profileId}:\n${output}`);
}

/** One JSON-RPC round trip over the emulator's WebSocket. */
async function callRpc(rpcUrl, method, params = {}, appId) {
  const socket = new WebSocket(appId ? `${rpcUrl}/rpc?appId=${appId}` : `${rpcUrl}/rpc`);
  try {
    await new Promise((resolvePromise, reject) => {
      socket.addEventListener("open", resolvePromise, { once: true });
      socket.addEventListener("error", () => reject(new Error(`cannot open ${rpcUrl}`)), { once: true });
    });
    const reply = new Promise((resolvePromise) =>
      socket.addEventListener("message", (event) => resolvePromise(JSON.parse(event.data)), { once: true }),
    );
    socket.send(JSON.stringify({ jsonrpc: "2.0", id: 1, method, params }));
    return await reply;
  } finally {
    socket.close();
  }
}

for (const profile of PROFILES) {
  test(`the ${profile.id} profile starts, serves the shell and answers RPC`, { skip: skipReason }, async (context) => {
    const emulator = await startEmulator(profile.id);
    context.after(() => emulator.child.kill("SIGTERM"));

    // The window frame and the shell are both served over HTTP.
    assert.equal((await fetch(`${emulator.http}/`)).status, 200);
    const shell = await fetch(`${emulator.http}/shell/`);
    assert.equal(shell.status, 200);
    assert.match(await shell.text(), /<html/i);

    // The real shell reads this to find the socket instead of assuming 8730.
    const discovery = await (await fetch(`${emulator.http}/system.json`)).json();
    assert.equal(discovery.rpcUrl, `${emulator.rpc}/rpc`);

    // The mock HAL answers, and reports this profile's screen.
    const info = await callRpc(emulator.rpc, "system.info");
    assert.equal(info.result.device, profile.id);
    assert.deepEqual(info.result.screen, {
      width: profile.width,
      height: profile.height,
      shape: profile.shape,
    });

    const power = await callRpc(emulator.rpc, "power.status");
    assert.equal(typeof power.result.percent, "number");
    const reading = await callRpc(emulator.rpc, "sensors.read", { sensor: "heartRate" });
    assert.equal(reading.result.unit, "bpm");
  });
}

test("an app-bound connection is held to its manifest's capabilities", { skip: skipReason }, async (context) => {
  const emulator = await startEmulator("pi-round-480");
  context.after(() => emulator.child.kill("SIGTERM"));

  // The bundled watchface declares system and power, never wallet.
  const denied = await callRpc(emulator.rpc, "wallet.publicKey", {}, "tech.solwear.watchface");
  assert.equal(denied.error.code, -32001);
});

test("the emulator prefers the shell the OS team builds", { skip: skipReason }, async (context) => {
  if (!existsSync(join(monorepo, "os", "shell", "dist", "index.html"))) {
    context.skip("os/shell has not been built in this checkout");
    return;
  }
  const emulator = await startEmulator("pi-round-480");
  context.after(() => emulator.child.kill("SIGTERM"));
  assert.match(emulator.output(), /shell\s+os\/shell\/dist/);
});
