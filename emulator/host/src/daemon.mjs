/**
 * The mock `solweard`: the JSON-RPC 2.0 surface from section 4.2 of the
 * architecture, backed by the mock HAL.
 *
 * When a real `solweard` binary is available the emulator runs that instead
 * (see server.mjs). This implementation exists so the whole developer loop
 * works on a Mac with nothing but Node installed, and so the shell and the SDK
 * can be exercised against the exact method names and error codes the device
 * will use.
 *
 * Caller identity: a client says who it is with a query parameter on the
 * WebSocket URL, `ws://127.0.0.1:8730/?app=<id>`. The shell opens one socket
 * per app frame, plus one for itself as `system`. Capability enforcement then
 * happens here, in the daemon, and not only in the shell.
 */

import { createHash, generateKeyPairSync, randomUUID, sign as edSign } from "node:crypto";
import { KNOWN_SENSORS, MockHal } from "./mock-hal.mjs";

export const ERR_PARSE = -32700;
export const ERR_INVALID_REQUEST = -32600;
export const ERR_METHOD_NOT_FOUND = -32601;
export const ERR_INVALID_PARAMS = -32602;
export const ERR_INTERNAL = -32603;
export const ERR_CAPABILITY_DENIED = -32001;
export const ERR_USER_REJECTED = -32002;

/** base58, for Solana-style public keys. */
const B58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

export function base58(buffer) {
  let value = 0n;
  for (const byte of buffer) value = value * 256n + BigInt(byte);
  let out = "";
  while (value > 0n) {
    out = B58[Number(value % 58n)] + out;
    value /= 58n;
  }
  for (const byte of buffer) {
    if (byte !== 0) break;
    out = `1${out}`;
  }
  return out || "1";
}

export class MockDaemon {
  /**
   * @param {object} options
   * @param {object} options.profile device profile JSON
   * @param {object[]} options.apps installed app manifests
   * @param {object} [options.mock] scripted HAL values
   */
  constructor({ profile, apps, mock }) {
    this.profile = profile;
    this.hal = new MockHal(profile, mock);
    this.apps = new Map();
    for (const manifest of apps) this.apps.set(manifest.id, manifest);

    this.notifications = [];
    this.launchListeners = new Set();

    // A throwaway wallet. It is regenerated on every start precisely so that
    // nobody can be tempted to fund it: a signature produced here is for
    // testing the flow, and means nothing on chain.
    const { privateKey, publicKey } = generateKeyPairSync("ed25519");
    this.walletPrivateKey = privateKey;
    this.walletPublicKeyRaw = publicKey.export({ format: "der", type: "spki" }).subarray(-32);
    this.walletAddress = base58(this.walletPublicKeyRaw);

    /**
     * Set by the server to the function that asks the wearer to confirm.
     * The daemon owns the decision to sign; the shell only renders the prompt.
     * @type {null | ((request: object) => Promise<boolean>)}
     */
    this.confirm = null;
    this.confirmResponse = null;
  }

  /** Register an app so it appears in apps.list and gets its capabilities. */
  installApp(manifest) {
    this.apps.set(manifest.id, manifest);
  }

  capabilitiesFor(appId) {
    if (appId === "system") return ["system", "power", "display", "sensors", "notifications", "apps", "wallet", "shell"];
    return this.apps.get(appId)?.capabilities ?? [];
  }

  /**
   * Handle one JSON-RPC request object.
   * @param {object} request
   * @param {string} callerId the app id from the socket URL
   * @returns {Promise<object|null>} the response, or null for a notification
   */
  async handle(request, callerId) {
    if (request?.jsonrpc !== "2.0" || typeof request.method !== "string") {
      return this.error(request?.id ?? null, ERR_INVALID_REQUEST, "not a JSON-RPC 2.0 request object");
    }
    const { id, method } = request;
    const isNotification = id === undefined;
    const params = request.params ?? {};

    if (Array.isArray(params)) {
      return isNotification
        ? null
        : this.error(id, ERR_INVALID_PARAMS, "parameters must be an object, never a positional array");
    }

    const namespace = method.split(".")[0];
    if (!this.capabilitiesFor(callerId).includes(namespace)) {
      return isNotification
        ? null
        : this.error(
            id,
            ERR_CAPABILITY_DENIED,
            `"${callerId}" has not been granted the "${namespace}" capability`,
          );
    }

    try {
      const result = await this.dispatch(method, params, callerId);
      return isNotification ? null : { jsonrpc: "2.0", id, result };
    } catch (error) {
      if (isNotification) return null;
      const code = typeof error.rpcCode === "number" ? error.rpcCode : ERR_INTERNAL;
      return this.error(id, code, error.message);
    }
  }

  error(id, code, message) {
    return { jsonrpc: "2.0", id, error: { code, message } };
  }

  static rpcError(code, message) {
    const error = new Error(message);
    error.rpcCode = code;
    return error;
  }

  async dispatch(method, params, callerId) {
    switch (method) {
      case "system.info":
        return this.hal.info();
      case "system.time":
        return this.hal.time();
      case "system.network":
        return { connected: true, ssid: "SolWear Emulator", signal: 100 };

      case "power.status":
        return this.hal.power();

      case "display.setBrightness": {
        if (typeof params.percent !== "number") {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, '"percent" must be a number');
        }
        const result = this.hal.setBrightness(params.percent);
        this.broadcast?.("display.brightnessChanged", { percent: this.hal.brightness });
        return result;
      }
      case "display.getBrightness":
        return { percent: this.hal.brightness };

      case "sensors.read": {
        if (typeof params.sensor !== "string") {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, '"sensor" must be a string');
        }
        const reading = this.hal.read(params.sensor);
        if (!reading) {
          throw MockDaemon.rpcError(
            ERR_INVALID_PARAMS,
            `unknown sensor "${params.sensor}"; this device has: ${KNOWN_SENSORS.join(", ")}`,
          );
        }
        return reading;
      }

      case "notifications.list":
        return { items: [...this.notifications].sort((a, b) => b.timestampMs - a.timestampMs) };

      case "notifications.post": {
        if (typeof params.title !== "string" || typeof params.body !== "string") {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, '"title" and "body" must be strings');
        }
        const notification = {
          id: randomUUID(),
          title: params.title,
          body: params.body,
          // The daemon stamps the real caller, so an app cannot post as another.
          appId: callerId === "system" ? (params.appId ?? "system") : callerId,
          timestampMs: Date.now(),
          read: false,
        };
        this.notifications.push(notification);
        if (this.notifications.length > 50) this.notifications.shift();
        this.broadcast?.("notifications.posted", { notification });
        return { id: notification.id };
      }

      case "apps.list":
        return {
          apps: [...this.apps.values()].map((manifest) => ({
            id: manifest.id,
            name: manifest.name,
            version: manifest.version,
            type: manifest.type,
            entry: manifest.entry ?? "index.html",
            icon: manifest.icon,
            capabilities: manifest.capabilities ?? [],
            author: manifest.author,
            description: manifest.description,
            installedAtMs: manifest.installedAtMs ?? this.hal.startedAt,
            signed: manifest.signed ?? false,
            publisherKey: manifest.publisherKey,
            url: manifest.url ?? `/apps/${manifest.id}/${manifest.entry ?? "index.html"}`,
          })),
        };

      case "apps.install":
        // Installing in the host emulator would mean unpacking a .swa into a
        // scratch directory and serving it, which is what the QEMU path and a
        // real device are for. Saying so beats pretending it worked.
        throw MockDaemon.rpcError(
          ERR_INTERNAL,
          "the host emulator does not install packages; use `solwear run` on the package's project, " +
            "or `solwear install --device` for a real watch",
        );

      case "apps.uninstall": {
        if (!this.apps.has(params.appId)) {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, `"${params.appId}" is not installed`);
        }
        this.apps.delete(params.appId);
        this.broadcast?.("apps.changed", { reason: "uninstalled", appId: params.appId });
        return {};
      }

      case "apps.launch": {
        if (!this.apps.has(params.appId)) {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, `"${params.appId}" is not installed`);
        }
        for (const listener of this.launchListeners) listener(params.appId);
        this.broadcast?.("apps.launch", { appId: params.appId });
        return {};
      }

      case "wallet.publicKey":
        return { publicKey: this.walletAddress };

      case "wallet.signTransaction": {
        if (typeof params.message !== "string") {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, '"message" must be a base64 string');
        }
        const appId = callerId === "system" ? (params.appId ?? "system") : callerId;

        // Never sign without an affirmative action from the wearer. The
        // emulator asks the shell to show the same prompt the device shows.
        let bytes;
        try {
          bytes = Buffer.from(params.message, "base64");
        } catch {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, '"message" must be valid base64');
        }
        const approved = this.confirm
          ? await this.confirm({
              appId,
              summary: {
                appId,
                byteLength: bytes.length,
                encoding: "base64",
                digest: createHash("sha256").update(bytes).digest("hex"),
                publicKey: this.walletAddress,
                label: params.label ?? null,
              },
            })
          : false;
        if (!approved) {
          throw MockDaemon.rpcError(ERR_USER_REJECTED, "the wearer declined the signing request");
        }

        const signature = edSign(null, bytes, this.walletPrivateKey);
        return { signature: base58(signature) };
      }

      case "shell.ready":
        return { version: "0.1.0-emulator", screen: this.profile.screen, apps: [...this.apps.values()] };

      case "shell.sensors":
        return { sensors: KNOWN_SENSORS };

      case "shell.confirmResponse": {
        if (typeof params.requestId !== "string" || typeof params.approved !== "boolean") {
          throw MockDaemon.rpcError(ERR_INVALID_PARAMS, '"requestId" and boolean "approved" are required');
        }
        return { delivered: this.confirmResponse?.(params.requestId, params.approved) ?? false };
      }

      default:
        throw MockDaemon.rpcError(ERR_METHOD_NOT_FOUND, `no such method "${method}"`);
    }
  }
}
