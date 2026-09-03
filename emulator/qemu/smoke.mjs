#!/usr/bin/env node

const httpPort = Number(process.argv[2] ?? 8731);
const rpcPort = Number(process.argv[3] ?? 8730);
const timeoutMs = Number(process.env.SOLWEAR_SMOKE_TIMEOUT_MS ?? 5000);

const systemDocument = await fetch(`http://127.0.0.1:${httpPort}/system.json`, {
  signal: AbortSignal.timeout(timeoutMs),
}).then((response) => {
  if (!response.ok) throw new Error(`system.json returned HTTP ${response.status}`);
  return response.json();
});

const socket = new WebSocket(`ws://127.0.0.1:${rpcPort}/rpc`);
await new Promise((resolve, reject) => {
  const timeout = setTimeout(() => reject(new Error("WebSocket open timed out")), timeoutMs);
  socket.addEventListener("open", () => { clearTimeout(timeout); resolve(); }, { once: true });
  socket.addEventListener("error", () => { clearTimeout(timeout); reject(new Error("WebSocket open failed")); }, { once: true });
});

let nextId = 1;
function call(method, params = {}) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error(`${method} timed out`)), timeoutMs);
    const receive = (event) => {
      const message = JSON.parse(event.data);
      if (message.id !== id) return;
      clearTimeout(timeout);
      socket.removeEventListener("message", receive);
      if (message.error) reject(new Error(`${method}: ${message.error.message}`));
      else resolve(message.result);
    };
    socket.addEventListener("message", receive);
    socket.send(JSON.stringify({ jsonrpc: "2.0", id, method, params }));
  });
}

const info = await call("system.info");
const stats = await call("system.stats");
const apps = await call("apps.list");
const nfc = await call("nfc.status");
const wallet = await call("wallet.status");
socket.close();

if (stats.platform.os !== "linux" || stats.platform.arch !== "aarch64") {
  throw new Error(`expected linux/aarch64, got ${stats.platform.os}/${stats.platform.arch}`);
}
if (apps.apps.length !== 5) throw new Error(`expected 5 apps, got ${apps.apps.length}`);

process.stdout.write(`${JSON.stringify({
  systemDocument,
  device: info.device,
  platform: stats.platform,
  uptimeMs: stats.uptimeMs,
  memory: stats.memory,
  storage: stats.storage,
  apps: apps.apps.map((app) => `${app.id}@${app.version}`),
  nfc,
  wallet: { protected: wallet.protected, locked: wallet.locked, publicKey: wallet.publicKey },
}, null, 2)}\n`);
