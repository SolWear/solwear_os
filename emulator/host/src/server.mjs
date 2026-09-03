/**
 * The emulator's HTTP and JSON-RPC server.
 *
 * Ports match the device exactly, because the shell and the SDK must not have
 * to know where they are running: static assets on 127.0.0.1:8731, JSON-RPC
 * over a WebSocket on 127.0.0.1:8730. Only the loopback interface is bound; the
 * mock wallet will sign anything the wearer confirms, and that is not something
 * to expose to the network.
 */

import { createReadStream, existsSync, readFileSync, statSync, watch } from "node:fs";
import { createServer } from "node:http";
import { extname, join, normalize, resolve, sep } from "node:path";
import { MockDaemon } from "./daemon.mjs";
import { MockHal } from "./mock-hal.mjs";
import { attachWebSocket } from "./ws.mjs";

/**
 * Headers for anything an app document loads.
 *
 * Apps run in a sandbox without `allow-same-origin`, so the document has an
 * opaque origin and fetches its own module bundle as a cross-origin request.
 * Without this header the browser blocks the app's own script, exactly as the
 * daemon has to allow it on the device.
 */
const APP_ASSET_HEADERS = { "access-control-allow-origin": "*" };

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".svg": "image/svg+xml",
  ".woff2": "font/woff2",
  ".map": "application/json; charset=utf-8",
  ".txt": "text/plain; charset=utf-8",
};

export class EmulatorServer {
  /**
   * @param {object} options
   * @param {object} options.profile
   * @param {string} options.webDir the emulator's own pages
   * @param {string} options.shellDir the shell to serve
   * @param {string} options.appDir the built app to serve
   * @param {object} options.appManifest
   * @param {object[]} options.systemApps other manifests to list
   * @param {number} options.httpPort
   * @param {number} options.rpcPort
   * @param {object} [options.mock]
   */
  constructor(options) {
    this.options = options;
    this.profile = options.profile;
    this.daemon = new MockDaemon({
      profile: options.profile,
      apps: [options.appManifest, ...(options.systemApps ?? [])].filter(Boolean),
      mock: options.mock,
    });

    /** Live-reload listeners, one per open emulator window. */
    this.reloadClients = new Set();
    /** Every open RPC socket, so the daemon can push notifications. */
    this.sockets = new Set();
    this.daemon.broadcast = (method, params) => this.broadcast(method, params);
    this.daemon.confirm = (request) => this.askForConfirmation(request);
    this.daemon.confirmResponse = (id, approved) => this.resolveConfirmation(id, approved);
    /** Pending wallet confirmations, keyed by request id. */
    this.confirmations = new Map();
    this.nextConfirmId = 1;
    this.startedAt = Date.now();
    this.rpcCount = 0;
    this.rpcErrors = 0;
    this.devLog = [];

    this.http = createServer((request, response) => this.onRequest(request, response));
    this.rpc = createServer((_request, response) => {
      response.writeHead(426, { "content-type": "text/plain" });
      response.end("This port speaks JSON-RPC over WebSocket. Connect with ws://127.0.0.1:8730/?app=<id>\n");
    });
    attachWebSocket(this.rpc, (connection) => this.onConnection(connection));
  }

  async listen() {
    const [httpPort, rpcPort] = await Promise.all([
      once(this.http, this.options.httpPort, "HTTP server", "--port"),
      once(this.rpc, this.options.rpcPort, "JSON-RPC server", "--rpc-port"),
    ]);
    this.options.httpPort = httpPort;
    this.options.rpcPort = rpcPort;
  }

  get url() {
    return `http://127.0.0.1:${this.options.httpPort}/`;
  }

  close() {
    for (const socket of this.sockets) socket.close(1001, "emulator stopping");
    this.http.close();
    this.rpc.close();
  }

  // ---- JSON-RPC ----------------------------------------------------------

  onConnection(connection) {
    const boundAppId = connection.url.searchParams.get("appId") ?? connection.url.searchParams.get("app");
    this.sockets.add(connection);
    connection.on("close", () => this.sockets.delete(connection));

    connection.on("message", async (text) => {
      let request;
      try {
        request = JSON.parse(text);
      } catch {
        connection.sendJson({ jsonrpc: "2.0", id: null, error: { code: -32700, message: "invalid JSON" } });
        return;
      }

      // A reply to a daemon-initiated request, such as a wallet confirmation.
      if (request.id !== undefined && request.method === undefined) {
        const pending = this.confirmations.get(request.id);
        if (pending) {
          this.confirmations.delete(request.id);
          pending(request.result?.approved === true);
        }
        return;
      }

      const stampedAppId = typeof request.appId === "string" ? request.appId : undefined;
      if (boundAppId && stampedAppId && boundAppId !== stampedAppId) {
        connection.sendJson({ jsonrpc: "2.0", id: request.id ?? null, error: { code: -32600, message: "appId does not match the connection identity" } });
        return;
      }
      const callerId = boundAppId ?? stampedAppId ?? "system";
      this.rpcCount += 1;
      const before = Date.now();
      const response = await this.daemon.handle(request, callerId);
      if (response?.error) this.rpcErrors += 1;
      this.record({
        at: Date.now(),
        caller: callerId,
        method: request.method,
        ok: !response?.error,
        durationMs: Date.now() - before,
        error: response?.error?.message,
      });
      if (response) connection.sendJson(response);
    });
  }

  broadcast(method, params) {
    for (const socket of this.sockets) {
      socket.sendJson({ jsonrpc: "2.0", method, params });
    }
  }

  /**
   * Ask the shell to show the signing prompt.
   *
   * The daemon sends a JSON-RPC request to the shell's own socket and waits for
   * the answer, which is how the device behaves: the decision belongs to the
   * daemon, the pixels belong to the shell. No prompt, no signature.
   */
  askForConfirmation(request) {
    const shellSocket = [...this.sockets].find((socket) =>
      !socket.url.searchParams.get("app") && !socket.url.searchParams.get("appId"),
    );
    if (!shellSocket) return Promise.resolve(false);

    const id = `confirm-${this.nextConfirmId++}`;
    return new Promise((resolve) => {
      const timeout = setTimeout(() => {
        if (this.confirmations.delete(id)) resolve(false);
      }, 60000);
      this.confirmations.set(id, (approved) => {
        clearTimeout(timeout);
        resolve(approved);
      });
      shellSocket.sendJson({ jsonrpc: "2.0", method: "wallet.confirmRequest", params: { requestId: id, ...request } });
    });
  }

  resolveConfirmation(id, approved) {
    const pending = this.confirmations.get(id);
    if (!pending) return false;
    this.confirmations.delete(id);
    pending(approved === true);
    return true;
  }

  // ---- HTTP --------------------------------------------------------------

  onRequest(request, response) {
    const url = new URL(request.url ?? "/", this.url);
    const path = decodeURIComponent(url.pathname);

    // The real shell asks the server it was loaded from where the JSON-RPC
    // socket is, exactly as it does on a device. Answering here is what lets
    // `--rpc-port` work with the real shell and not only the reference one.
    if (path === "/system.json") {
      return this.sendJson(response, {
        version: "0.1.0-emulator",
        rpcAddr: `127.0.0.1:${this.options.rpcPort}`,
        httpAddr: `127.0.0.1:${this.options.httpPort}`,
        rpcUrl: `ws://127.0.0.1:${this.options.rpcPort}/rpc`,
        httpUrl: this.url,
      });
    }
    if (path === "/emulator/state.json") return this.sendJson(response, this.state());
    if (path === "/emulator/devtools.json") return this.sendJson(response, this.devtools());
    if (path === "/emulator/control") return this.control(response, url.searchParams);
    if (path === "/emulator/profile") return this.switchProfile(response, url.searchParams.get("id"));
    if (path === "/emulator/reload") return this.subscribeToReloads(response);

    if (path === "/" || path === "/index.html") {
      return this.sendFile(response, join(this.options.webDir, "index.html"));
    }
    if (path.startsWith("/emulator/")) {
      return this.sendFromDirectory(response, this.options.webDir, path.slice("/emulator/".length));
    }
    if (path === "/shell" || path === "/shell/") {
      return this.sendFile(response, join(this.options.shellDir, "index.html"));
    }
    if (path.startsWith("/shell/")) {
      return this.sendFromDirectory(response, this.options.shellDir, path.slice("/shell/".length));
    }
    if (path === "/app" || path === "/app/") {
      return this.sendFile(
        response,
        join(this.options.appDir, this.options.appManifest.entry ?? "index.html"),
        APP_ASSET_HEADERS,
      );
    }
    if (path.startsWith("/app/")) {
      return this.sendFromDirectory(response, this.options.appDir, path.slice("/app/".length), APP_ASSET_HEADERS);
    }
    if (path.startsWith("/apps/")) {
      const [, , appId, ...rest] = path.split("/");
      const root = this.options.appRoots?.[appId];
      if (!root) return this.sendText(response, 404, `No assets for app ${appId}.`);
      return this.sendFromDirectory(response, root, rest.join("/") || "index.html", APP_ASSET_HEADERS);
    }

    this.sendText(response, 404, `Nothing is served at ${path}.`);
  }

  /** Everything the emulator window and the shell need to configure themselves. */
  state() {
    return {
      profile: this.profile,
      profiles: this.options.profiles ?? [],
      rpcUrl: `ws://127.0.0.1:${this.options.rpcPort}/`,
      app: this.options.appManifest,
      shellSource: this.options.shellSource,
      systemApps: this.options.systemApps ?? [],
    };
  }

  record(entry) {
    this.devLog.push(entry);
    if (this.devLog.length > 100) this.devLog.splice(0, this.devLog.length - 100);
  }

  devtools() {
    return {
      uptimeMs: Date.now() - this.startedAt,
      rpcCount: this.rpcCount,
      rpcErrors: this.rpcErrors,
      connections: this.sockets.size,
      reloadClients: this.reloadClients.size,
      profile: this.profile.id,
      app: { id: this.options.appManifest.id, name: this.options.appManifest.name },
      hal: this.daemon.hal.snapshot(),
      notifications: this.daemon.notifications.length,
      apps: this.daemon.apps.size,
      wallet: this.daemon.walletAddress,
      nfc: { enabled: this.daemon.nfcEnabled, ready: true, backend: "mock-pn532" },
      log: this.devLog.slice(-30).reverse(),
    };
  }

  control(response, params) {
    try {
      const name = params.get("name");
      const value = params.get("value");
      if (name === "notification") {
        const notification = {
          id: `dev-${Date.now()}`,
          title: value || "Developer notification",
          body: "Injected from the emulator cockpit",
          appId: "system",
          timestampMs: Date.now(),
          read: false,
        };
        this.daemon.notifications.push(notification);
        this.daemon.broadcast?.("notifications.posted", { notification });
        this.record({ at: Date.now(), caller: "developer", method: "notifications.inject", ok: true, durationMs: 0 });
      } else if (name === "nfc") {
        this.daemon.nfcEnabled = value === "true" || value === "1";
        this.daemon.broadcast?.("nfc.statusChanged", { enabled: this.daemon.nfcEnabled });
        this.record({ at: Date.now(), caller: "developer", method: "nfc.setEnabled", ok: true, durationMs: 0 });
      } else if (name) {
        this.daemon.hal.control(name, value);
        if (name === "brightness") this.daemon.broadcast?.("display.brightnessChanged", { percent: this.daemon.hal.brightness });
        this.record({ at: Date.now(), caller: "developer", method: `hal.${name}`, ok: true, durationMs: 0 });
      } else {
        throw new Error("missing control name");
      }
      this.sendJson(response, this.devtools());
    } catch (error) {
      this.sendText(response, 400, error.message);
    }
  }

  /**
   * Change the device profile without restarting.
   *
   * Checking a layout on a round 240 and then on a wide 800 is the single most
   * common thing a developer does in this emulator, so it must not cost a
   * restart. The daemon's HAL is rebuilt too, otherwise system.info would keep
   * reporting the old screen.
   */
  switchProfile(response, id) {
    const profile = (this.options.allProfiles ?? []).find((candidate) => candidate.id === id);
    if (!profile) {
      return this.sendText(response, 404, `No device profile called "${id}".`);
    }
    this.profile = profile;
    this.options.profile = profile;
    this.daemon.profile = profile;
    this.daemon.hal = new MockHal(profile, this.options.mock);
    this.sendJson(response, this.state());
    this.notifyReload("profile changed");
  }

  subscribeToReloads(response) {
    response.writeHead(200, {
      "content-type": "text/event-stream",
      "cache-control": "no-cache",
      connection: "keep-alive",
    });
    response.write("retry: 500\n\n");
    this.reloadClients.add(response);
    response.on("close", () => this.reloadClients.delete(response));
  }

  notifyReload(reason) {
    for (const client of this.reloadClients) client.write(`data: ${JSON.stringify({ reason })}\n\n`);
  }

  /** Watch the app build output and reload the window when it changes. */
  watchApp() {
    if (!existsSync(this.options.appDir)) return;
    let timer = null;
    const trigger = () => {
      clearTimeout(timer);
      // Debounced, because a build writes several files in quick succession
      // and each one would otherwise cost a page reload.
      timer = setTimeout(() => this.notifyReload("app rebuilt"), 120);
    };
    try {
      watch(this.options.appDir, { recursive: true }, trigger);
    } catch {
      watch(this.options.appDir, trigger);
    }
  }

  // ---- static file helpers ----------------------------------------------

  sendFromDirectory(response, root, relativePath, extraHeaders) {
    // Resolve and then check containment, so ".." cannot escape the directory.
    const target = resolve(root, normalize(relativePath));
    if (target !== resolve(root) && !target.startsWith(resolve(root) + sep)) {
      return this.sendText(response, 403, "Refusing to serve a path outside the served directory.");
    }
    this.sendFile(response, target, extraHeaders);
  }

  sendFile(response, path, extraHeaders) {
    let stats;
    try {
      stats = statSync(path);
    } catch {
      return this.sendText(response, 404, `Not found: ${path}`);
    }
    if (stats.isDirectory()) return this.sendFile(response, join(path, "index.html"), extraHeaders);

    response.writeHead(200, {
      "content-type": MIME[extname(path).toLowerCase()] ?? "application/octet-stream",
      "content-length": stats.size,
      "cache-control": "no-store",
      ...extraHeaders,
    });
    createReadStream(path).pipe(response);
  }

  sendJson(response, value) {
    const body = JSON.stringify(value);
    response.writeHead(200, {
      "content-type": "application/json; charset=utf-8",
      "content-length": Buffer.byteLength(body),
      "cache-control": "no-store",
    });
    response.end(body);
  }

  sendText(response, status, text) {
    response.writeHead(status, { "content-type": "text/plain; charset=utf-8" });
    response.end(`${text}\n`);
  }
}

function once(server, port, label, flag) {
  return new Promise((resolvePromise, reject) => {
    server.once("error", (error) => {
      if (error.code === "EADDRINUSE") {
        // Name the listener that could not bind and the flag that moves it.
        // Saying "--port" when the JSON-RPC socket is the one in conflict
        // sends people to change the wrong number.
        reject(
          new Error(
            `the ${label} cannot bind port ${port}: it is already in use. ` +
              `Another emulator or a running solweard is probably holding it. ` +
              `Stop it, or pass ${flag} <number> to move the ${label}.`,
          ),
        );
      } else reject(error);
    });
    server.listen(port, "127.0.0.1", () => resolvePromise(server.address().port));
  });
}

export function readJson(path) {
  return JSON.parse(readFileSync(path, "utf8"));
}
