/**
 * @solwear/sdk — the application API for SolWear OS.
 *
 *   import { solwear } from "@solwear/sdk";
 *
 *   await solwear.ready();
 *   const battery = await solwear.power.status();
 *   solwear.on("tick", (t) => render(t));
 *   const screen = solwear.system.screen;
 */

import { Bridge, type BridgeContext } from "./bridge.js";
import {
  AppsClient,
  DisplayClient,
  NotificationsClient,
  PowerClient,
  SensorsClient,
  SystemClient,
  WalletClient,
  NfcClient,
} from "./clients.js";
import type { Listener } from "./emitter.js";
import type { EventName, SolwearEvents } from "./types.js";

export const SDK_VERSION = "0.1.0";

export class Solwear {
  private readonly bridge: Bridge;

  readonly system: SystemClient;
  readonly power: PowerClient;
  readonly display: DisplayClient;
  readonly sensors: SensorsClient;
  readonly notifications: NotificationsClient;
  readonly apps: AppsClient;
  readonly wallet: WalletClient;
  readonly nfc: NfcClient;

  constructor(bridge: Bridge = new Bridge(SDK_VERSION)) {
    this.bridge = bridge;
    this.system = new SystemClient(bridge);
    this.power = new PowerClient(bridge);
    this.display = new DisplayClient(bridge);
    this.sensors = new SensorsClient(bridge);
    this.notifications = new NotificationsClient(bridge);
    this.apps = new AppsClient(bridge);
    this.wallet = new WalletClient(bridge);
    this.nfc = new NfcClient(bridge);
  }

  /**
   * Resolves once the shell has handed over the app context. Call this before
   * the first render so that `system.screen` and `capabilities` are populated.
   */
  ready(): Promise<BridgeContext> {
    return this.bridge.ready();
  }

  /** This app's id, as declared in manifest.json. */
  get appId(): string {
    return this.bridge.current?.appId ?? "unknown.app";
  }

  /** Capabilities granted to this app. */
  get capabilities(): readonly string[] {
    return this.bridge.current?.capabilities ?? [];
  }

  /** True while this app is the surface on screen. */
  get visible(): boolean {
    return this.bridge.current?.visible ?? true;
  }

  /** Subscribe to a system event. Returns an unsubscribe function. */
  on<K extends EventName>(event: K, listener: Listener<SolwearEvents[K]>): () => void {
    return this.bridge.events.on(event, listener);
  }

  /** Subscribe to the next occurrence of a system event only. */
  once<K extends EventName>(event: K, listener: Listener<SolwearEvents[K]>): () => void {
    return this.bridge.events.once(event, listener);
  }

  /** Unsubscribe a listener registered with `on`. */
  off<K extends EventName>(event: K, listener: Listener<SolwearEvents[K]>): void {
    this.bridge.events.off(event, listener);
  }

  /**
   * Escape hatch for methods added to the daemon after this SDK release.
   * Capability rules still apply.
   */
  call<T = unknown>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    return this.bridge.call<T>(method, params);
  }
}

/** The singleton every app uses. */
export const solwear = new Solwear();

export { Bridge, type BridgeContext } from "./bridge.js";
export { TypedEmitter, type Listener } from "./emitter.js";
export { SolwearBridgeError, SolwearRpcError } from "./errors.js";
export * from "./protocol.js";
export * from "./types.js";
export {
  AppsClient,
  DisplayClient,
  NotificationsClient,
  PowerClient,
  SensorsClient,
  SystemClient,
  WalletClient,
  NfcClient,
} from "./clients.js";
export { layout, type LayoutMetrics } from "./layout.js";
