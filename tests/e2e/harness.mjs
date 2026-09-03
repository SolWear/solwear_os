/**
 * Support code for the end-to-end test: starting a real `solweard`, talking to
 * it over the real JSON-RPC WebSocket, and building real `.swa` packages with
 * the real CLI.
 *
 * Nothing here fakes a component. The daemon is the compiled Rust binary, the
 * packages are produced by `sdk/cli`, and the transport is the socket the shell
 * uses on a device. The only substitutions are the ones the architecture
 * already provides for: `SOLWEAR_HAL=mock` and a temporary data directory.
 */

import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { setTimeout as delay } from "node:timers/promises";

export const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..");
export const cliBin = join(repoRoot, "sdk", "cli", "dist", "bin.js");

/** Where cargo puts the daemon. A release build is preferred when both exist. */
export function daemonBinary() {
  const candidates = [
    join(repoRoot, "os", "solweard", "target", "release", "solweard"),
    join(repoRoot, "os", "solweard", "target", "debug", "solweard"),
  ];
  return candidates.find((path) => existsSync(path));
}

/** Fail early and clearly when a prerequisite has not been built. */
export function requirePrerequisites() {
  const missing = [];
  if (!daemonBinary()) missing.push("os/solweard: cargo build --manifest-path os/solweard/Cargo.toml");
  if (!existsSync(cliBin)) missing.push("sdk/cli: npm --prefix sdk/cli run build");
  if (!existsSync(join(repoRoot, "sdk", "runtime", "dist", "index.js"))) {
    missing.push("sdk/runtime: npm --prefix sdk/runtime run build");
  }
  if (!existsSync(join(repoRoot, "os", "shell", "dist", "index.html"))) {
    missing.push("os/shell: npm --prefix os/shell run build");
  }
  if (typeof globalThis.WebSocket !== "function") {
    missing.push("Node 22 or newer: this test uses the global WebSocket client");
  }
  if (missing.length > 0) {
    throw new Error(`The end-to-end test needs these first:\n  - ${missing.join("\n  - ")}`);
  }
}

export function makeTempDir(prefix) {
  return mkdtempSync(join(tmpdir(), `solwear-${prefix}-`));
}

export function removeTempDir(path) {
  rmSync(path, { recursive: true, force: true });
}

/** Run a command to completion, throwing with its output when it fails. */
export function run(command, args, options = {}) {
  return new Promise((resolvePromise, reject) => {
    const child = spawn(command, args, { ...options, stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk) => (stdout += chunk));
    child.stderr.on("data", (chunk) => (stderr += chunk));
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) resolvePromise({ stdout, stderr });
      else reject(new Error(`${command} ${args.join(" ")} exited ${code}\n${stdout}${stderr}`));
    });
  });
}

export const solwear = (args, options = {}) => run(process.execPath, [cliBin, ...args], options);

/**
 * A running `solweard`.
 *
 * Both listeners are asked for port 0, so the operating system picks free
 * ports and the test can never collide with a daemon a developer already has
 * running. The daemon records what it bound in `runtime.json`, which is what we
 * read back rather than scraping the log.
 */
export class Daemon {
  constructor(dataDir) {
    this.dataDir = dataDir;
    this.child = null;
    this.runtime = null;
    this.log = "";
  }

  async start() {
    this.child = spawn(daemonBinary(), [], {
      cwd: repoRoot,
      env: {
        ...process.env,
        SOLWEAR_HAL: "mock",
        SOLWEAR_DATA_DIR: this.dataDir,
        SOLWEAR_SHELL_DIR: join(repoRoot, "os", "shell", "dist"),
        SOLWEAR_RPC_ADDR: "127.0.0.1:0",
        SOLWEAR_HTTP_ADDR: "127.0.0.1:0",
        SOLWEAR_LOG: "info",
      },
      stdio: ["ignore", "pipe", "pipe"],
    });
    this.child.stdout.on("data", (chunk) => (this.log += chunk));
    this.child.stderr.on("data", (chunk) => (this.log += chunk));
    this.child.on("exit", (code) => {
      this.exitCode = code;
    });

    const runtimePath = join(this.dataDir, "runtime.json");
    const deadline = Date.now() + 20_000;
    while (Date.now() < deadline) {
      if (this.exitCode !== undefined) {
        throw new Error(`solweard exited with ${this.exitCode} before it was ready:\n${this.log}`);
      }
      if (existsSync(runtimePath)) {
        try {
          this.runtime = JSON.parse(readFileSync(runtimePath, "utf8"));
          if (this.runtime.rpcUrl && this.runtime.httpUrl) return this.runtime;
        } catch {
          // Written but not yet flushed; try again.
        }
      }
      await delay(50);
    }
    throw new Error(`solweard did not become ready within 20s:\n${this.log}`);
  }

  get httpUrl() {
    return this.runtime.httpUrl.replace(/\/$/, "");
  }

  get rpcUrl() {
    return this.runtime.rpcUrl;
  }

  async stop() {
    if (!this.child || this.exitCode !== undefined) return;
    this.child.kill("SIGTERM");
    const deadline = Date.now() + 5000;
    while (this.exitCode === undefined && Date.now() < deadline) await delay(25);
    if (this.exitCode === undefined) this.child.kill("SIGKILL");
  }
}

/**
 * One JSON-RPC connection.
 *
 * Opened without a query parameter it is the privileged shell connection;
 * opened with `?appId=<id>` the daemon binds it to that app for its whole life,
 * which is what makes an app unable to claim someone else's identity.
 */
export class RpcConnection {
  constructor(url, appId) {
    this.url = appId ? `${url}?appId=${encodeURIComponent(appId)}` : url;
    this.appId = appId ?? null;
    this.nextId = 1;
    this.pending = new Map();
    this.events = [];
    this.eventWaiters = [];
  }

  static async open(url, appId) {
    const connection = new RpcConnection(url, appId);
    await connection.connect();
    return connection;
  }

  connect() {
    return new Promise((resolvePromise, reject) => {
      this.socket = new WebSocket(this.url);
      this.socket.addEventListener("open", () => resolvePromise(this));
      this.socket.addEventListener("error", () => reject(new Error(`cannot open ${this.url}`)));
      this.socket.addEventListener("message", (event) => this.receive(String(event.data)));
      this.socket.addEventListener("close", () => {
        for (const pending of this.pending.values()) pending.reject(new Error("connection closed"));
        this.pending.clear();
      });
    });
  }

  receive(text) {
    let message;
    try {
      message = JSON.parse(text);
    } catch {
      return;
    }
    if (message.id === undefined || message.id === null) {
      this.events.push(message);
      for (const waiter of this.eventWaiters.splice(0)) waiter();
      return;
    }
    const pending = this.pending.get(message.id);
    if (!pending) return;
    this.pending.delete(message.id);
    pending.resolve(message);
  }

  /** Send a request and resolve with the whole response envelope. */
  send(method, params = {}, extra = {}) {
    const id = this.nextId++;
    const message = { jsonrpc: "2.0", id, method, params, ...extra };
    return new Promise((resolvePromise, reject) => {
      this.pending.set(id, { resolve: resolvePromise, reject });
      this.socket.send(JSON.stringify(message));
    });
  }

  /** Send a request and resolve with `result`, throwing on a JSON-RPC error. */
  async call(method, params = {}, extra = {}) {
    const response = await this.send(method, params, extra);
    if (response.error) {
      const error = new Error(`${method}: ${response.error.message}`);
      error.rpc = response.error;
      throw error;
    }
    return response.result;
  }

  /** Resolve with the JSON-RPC error object, failing if the call succeeded. */
  async expectError(method, params = {}, extra = {}) {
    const response = await this.send(method, params, extra);
    if (!response.error) {
      throw new Error(`${method} was expected to fail but returned ${JSON.stringify(response.result)}`);
    }
    return response.error;
  }

  /** Wait for a pushed event, e.g. `wallet.confirmRequest`. */
  async waitForEvent(method, timeoutMs = 10_000) {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const index = this.events.findIndex((event) => event.method === method);
      if (index >= 0) return this.events.splice(index, 1)[0];
      if (Date.now() > deadline) throw new Error(`no ${method} event within ${timeoutMs}ms`);
      await new Promise((resolvePromise) => {
        this.eventWaiters.push(resolvePromise);
        setTimeout(resolvePromise, 50);
      });
    }
  }

  close() {
    this.socket?.close();
  }
}

/** Resolve once `promise` settles, or with the marker when `ms` elapses first. */
export async function settledWithin(promise, ms, marker = "pending") {
  return Promise.race([promise, delay(ms, marker)]);
}

const BASE58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

/** Decode a base58 string, which is how the daemon reports Solana keys. */
export function base58Decode(text) {
  let value = 0n;
  for (const character of text) {
    const digit = BASE58.indexOf(character);
    if (digit < 0) throw new Error(`"${character}" is not a base58 digit`);
    value = value * 58n + BigInt(digit);
  }
  const bytes = [];
  while (value > 0n) {
    bytes.unshift(Number(value % 256n));
    value /= 256n;
  }
  for (const character of text) {
    if (character !== "1") break;
    bytes.unshift(0);
  }
  return Buffer.from(bytes);
}

export { delay };
