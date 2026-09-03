/**
 * The postMessage transport. One instance per app frame.
 *
 * The bridge does three things: it performs the hello/init handshake so the
 * app learns its screen and capabilities synchronously after `ready()`, it
 * turns RPC calls into correlated messages, and it fans shell events out to
 * the emitter.
 */

import {
  BRIDGE_PROTOCOL,
  BRIDGE_VERSION,
  type Capability,
  type EventMessage,
  type InitMessage,
  type RpcResponseMessage,
  type ScreenInfo,
  type ShellToAppMessage,
} from "./protocol.js";
import { SolwearBridgeError, SolwearRpcError } from "./errors.js";
import { TypedEmitter } from "./emitter.js";
import type { SolwearEvents } from "./types.js";

/** How long to wait for the shell's `init` before declaring the app detached. */
const HANDSHAKE_TIMEOUT_MS = 3000;

export interface BridgeContext {
  appId: string;
  capabilities: Capability[];
  screen: ScreenInfo;
  device: string;
  osVersion: string;
  visible: boolean;
  /** False when no shell answered, e.g. the page was opened directly in a browser. */
  attached: boolean;
}

const DETACHED_HINT =
  "No SolWear shell answered the handshake. Apps must run inside the shell: " +
  "start them with `solwear run` (host emulator) or install them on a device. " +
  "Opening index.html directly in a browser leaves the system APIs unavailable.";

export class Bridge {
  readonly events = new TypedEmitter<SolwearEvents>();

  private nextId = 1;
  private pending = new Map<number, { resolve: (value: unknown) => void; reject: (error: Error) => void; method: string }>();
  private context: BridgeContext | null = null;
  private readyPromise: Promise<BridgeContext>;
  private resolveReady!: (context: BridgeContext) => void;
  private target: Window | null;
  private detachedTicker: ReturnType<typeof setInterval> | null = null;
  private handshakeTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(private readonly sdkVersion: string) {
    this.readyPromise = new Promise<BridgeContext>((resolve) => {
      this.resolveReady = resolve;
    });

    if (typeof window === "undefined") {
      // Node, a test runner, or a build step imported the SDK. Stay inert.
      this.target = null;
      this.settleDetached();
      return;
    }

    this.target = window.parent && window.parent !== window ? window.parent : null;
    window.addEventListener("message", this.onMessage);

    if (this.target) {
      this.send({ protocol: BRIDGE_PROTOCOL, type: "hello", solwear: BRIDGE_VERSION, kind: "hello", sdk: this.sdkVersion });
      this.forwardPointerEvents();
      this.handshakeTimer = setTimeout(() => {
        if (!this.context) this.settleDetached();
      }, HANDSHAKE_TIMEOUT_MS);
    } else {
      this.settleDetached();
    }
  }

  /** Resolves once the shell has answered, or once the app is known to be detached. */
  ready(): Promise<BridgeContext> {
    return this.readyPromise;
  }

  /** The handshake result, or null before `ready()` resolves. */
  get current(): BridgeContext | null {
    return this.context;
  }

  /** Issue one JSON-RPC call through the shell. */
  async call<T>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    const context = await this.ready();
    if (!context.attached || !this.target) throw new SolwearBridgeError(DETACHED_HINT);

    const namespace = method.split(".")[0] as Capability;
    if (!context.capabilities.includes(namespace)) {
      throw new SolwearRpcError(
        method,
        -32001,
        `this app did not declare the "${namespace}" capability. ` +
          `Add it to the "capabilities" array in manifest.json and reinstall.`,
      );
    }

    const id = this.nextId++;
    return await new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve: resolve as (value: unknown) => void, reject, method });
      this.send({ protocol: BRIDGE_PROTOCOL, type: "rpc", solwear: BRIDGE_VERSION, kind: "rpc", id, method, params });
    });
  }

  /**
   * Report pointer downs and ups to the shell so it can recognise gestures.
   * Only the two endpoints are sent, not every move: the shell needs a start,
   * an end and a duration, and streaming moves across a postMessage boundary
   * sixty times a second would cost more than the gesture is worth.
   */
  private forwardPointerEvents(): void {
    const report = (event: PointerEvent, phase: "down" | "up"): void => {
      this.send({
        protocol: BRIDGE_PROTOCOL,
        type: "pointer",
        solwear: BRIDGE_VERSION,
        kind: "pointer",
        phase,
        x: event.clientX / Math.max(1, window.innerWidth),
        y: event.clientY / Math.max(1, window.innerHeight),
        t: event.timeStamp,
      });
    };
    window.addEventListener("pointerdown", (event) => report(event, "down"), { passive: true, capture: true });
    window.addEventListener("pointerup", (event) => report(event, "up"), { passive: true, capture: true });
  }

  private send(message: unknown): void {
    // The app frame is sandboxed and therefore has an opaque origin, so a
    // specific targetOrigin cannot be used here. The shell is the only window
    // that receives it, and the shell checks the source frame before acting.
    this.target?.postMessage(message, "*");
  }

  private onMessage = (event: MessageEvent): void => {
    const data = event.data as ShellToAppMessage | undefined;
    if (!data || typeof data !== "object") return;
    if (data.protocol !== BRIDGE_PROTOCOL && (data as { solwear?: number }).solwear !== BRIDGE_VERSION) return;
    if (this.target && event.source !== this.target) return;

    const kind = data.type ?? (data as { kind?: string }).kind;
    switch (kind) {
      case "init":
        this.onInit(data as InitMessage);
        break;
      case "rpc-result":
      case "result":
      case "error":
        this.onResult(data as RpcResponseMessage);
        break;
      case "event":
        this.onEvent(data as EventMessage);
        break;
    }
  };

  private onInit(message: InitMessage): void {
    if (this.handshakeTimer) clearTimeout(this.handshakeTimer);
    this.handshakeTimer = null;
    const context: BridgeContext = {
      appId: message.appId,
      capabilities: message.capabilities ?? [],
      screen: message.screen,
      device: message.device ?? "solwear",
      osVersion: message.osVersion ?? "0.1.0",
      visible: message.visible ?? true,
      attached: true,
    };
    this.context = context;
    this.resolveReady(context);
  }

  private onResult(message: RpcResponseMessage): void {
    const entry = this.pending.get(message.id);
    if (!entry) return;
    this.pending.delete(message.id);
    if (message.error || message.kind === "error") {
      const error = message.error ?? { code: -32603, message: "shell returned an unspecified error" };
      entry.reject(
        new SolwearRpcError(entry.method, error.code, error.message, error.data),
      );
    } else {
      entry.resolve(message.result);
    }
  }

  private onEvent(message: EventMessage): void {
    if (message.event === "tick") {
      const tick = message.payload as { epochMs?: number; hours?: number; minutes?: number; seconds?: number };
      const now = new Date(tick.epochMs ?? Date.now());
      tick.epochMs = now.getTime();
      tick.hours ??= now.getHours();
      tick.minutes ??= now.getMinutes();
      tick.seconds ??= now.getSeconds();
    }
    if (message.event === "gesture") {
      const gesture = message.payload as { direction?: string; gesture?: string; x?: number; y?: number };
      if (!gesture.gesture && gesture.direction) gesture.gesture = `swipe-${gesture.direction}`;
      gesture.x ??= 0.5;
      gesture.y ??= 0.5;
    }
    if (message.event === "visibility" && this.context) {
      const payload = message.payload as { visible: boolean };
      this.context.visible = payload.visible;
    }
    this.events.emit(message.event as keyof SolwearEvents, message.payload as never);
  }

  /**
   * Fall back to a usable, obviously-fake context when no shell is present.
   * The screen is taken from the window so adaptive layout can still be tried
   * in a plain browser tab, and a local ticker keeps clocks moving; every RPC
   * call still fails loudly with an explanation.
   */
  private settleDetached(): void {
    if (this.context) return;
    const width = typeof window === "undefined" ? 480 : window.innerWidth || 480;
    const height = typeof window === "undefined" ? 480 : window.innerHeight || 480;
    this.context = {
      appId: "unknown.app",
      capabilities: [],
      screen: { width, height, shape: width === height ? "square" : "rect" },
      device: "detached",
      osVersion: "0.0.0",
      visible: true,
      attached: false,
    };
    this.resolveReady(this.context);
    if (typeof window !== "undefined" && !this.detachedTicker) {
      console.warn(`[solwear] ${DETACHED_HINT}`);
      this.detachedTicker = setInterval(() => {
        const now = new Date();
        this.events.emit("tick", {
          epochMs: now.getTime(),
          hours: now.getHours(),
          minutes: now.getMinutes(),
          seconds: now.getSeconds(),
        });
      }, 1000);
    }
  }
}
