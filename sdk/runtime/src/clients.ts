/** The typed namespace clients. Each one is a thin, documented wrapper over `bridge.call`. */

import type { Bridge } from "./bridge.js";
import type {
  AppRecord,
  Notification,
  PowerStatus,
  SensorName,
  SensorReading,
  SystemInfo,
  SystemStats,
  SystemTime,
  WalletPublicKey,
  WalletStatus,
  WalletActivity,
  WalletSignature,
  NfcStatus,
  NfcWalletRecord,
  NfcDiagnostics,
} from "./types.js";
import type { ScreenInfo } from "./protocol.js";

export class SystemClient {
  constructor(private readonly bridge: Bridge) {}

  /** Device name, OS version and screen geometry. */
  info(): Promise<SystemInfo> {
    return this.bridge.call<SystemInfo>("system.info");
  }

  /** Wall clock time and the device timezone. */
  time(): Promise<SystemTime> {
    return this.bridge.call<SystemTime>("system.time");
  }

  /** Runtime health and resource counters from the Linux daemon. */
  stats(): Promise<SystemStats> {
    return this.bridge.call<SystemStats>("system.stats");
  }

  /**
   * Screen geometry, available synchronously after `solwear.ready()`.
   * Layout code needs this on the first frame, which is why the shell sends it
   * in the handshake instead of making every app await an RPC call.
   */
  get screen(): ScreenInfo {
    const context = this.bridge.current;
    if (!context) {
      throw new Error(
        "solwear.system.screen was read before the shell handshake finished. " +
          "Await solwear.ready() first, or use `await solwear.system.info()`.",
      );
    }
    return context.screen;
  }
}

export class PowerClient {
  constructor(private readonly bridge: Bridge) {}

  /** Battery percentage, charge state and a runtime estimate in minutes. */
  status(): Promise<PowerStatus> {
    return this.bridge.call<PowerStatus>("power.status");
  }
}

export class DisplayClient {
  constructor(private readonly bridge: Bridge) {}

  /** Set backlight brightness. `percent` is clamped to 0-100 by the daemon. */
  async setBrightness(percent: number): Promise<void> {
    if (!Number.isFinite(percent)) throw new TypeError("setBrightness expects a number");
    await this.bridge.call<Record<string, never>>("display.setBrightness", {
      percent: Math.max(0, Math.min(100, Math.round(percent))),
    });
  }
}

export class SensorsClient {
  constructor(private readonly bridge: Bridge) {}

  /** Read one sensor. Under the mock HAL the values are deterministic. */
  read(sensor: SensorName | (string & {})): Promise<SensorReading> {
    return this.bridge.call<SensorReading>("sensors.read", { sensor });
  }
}

export class NotificationsClient {
  constructor(private readonly bridge: Bridge) {}

  /** All notifications currently in the tray, newest first. */
  async list(): Promise<Notification[]> {
    const result = await this.bridge.call<{ items: Notification[] }>("notifications.list");
    return result.items ?? [];
  }

  /** Post a notification. The daemon stamps it with the calling app's id. */
  async post(input: { title: string; body: string; appId?: string }): Promise<string> {
    const appId = input.appId ?? this.bridge.current?.appId ?? "unknown.app";
    const result = await this.bridge.call<{ id: string }>("notifications.post", {
      title: input.title,
      body: input.body,
      appId,
    });
    return result.id;
  }
}

export class AppsClient {
  constructor(private readonly bridge: Bridge) {}

  /** Every installed app. Requires the "apps" capability. */
  async list(): Promise<AppRecord[]> {
    const result = await this.bridge.call<{ apps: AppRecord[] }>("apps.list");
    return result.apps ?? [];
  }

  /** Install from a URL or a local path. The daemon verifies the signature. */
  install(
    source: string,
    integrity: { expectedSha256?: string; expectedPublisherKey?: string } = {},
  ): Promise<{ appId: string; version: string }> {
    return this.bridge.call<{ appId: string; version: string }>("apps.install", { source, ...integrity });
  }

  async uninstall(appId: string): Promise<void> {
    await this.bridge.call<Record<string, never>>("apps.uninstall", { appId });
  }

  async launch(appId: string): Promise<void> {
    await this.bridge.call<Record<string, never>>("apps.launch", { appId });
  }
}

export class WalletClient {
  constructor(private readonly bridge: Bridge) {}

  /** The device wallet's public key, base58 encoded. */
  async publicKey(): Promise<string> {
    const result = await this.bridge.call<WalletPublicKey>("wallet.publicKey");
    return result.publicKey;
  }

  status(): Promise<WalletStatus> {
    return this.bridge.call<WalletStatus>("wallet.status");
  }

  async setPassphrase(passphrase: string, name = "SolWear"): Promise<void> {
    await this.bridge.call<Record<string, never>>("wallet.setPassphrase", { passphrase, name });
  }

  async lock(): Promise<void> {
    await this.bridge.call<Record<string, never>>("wallet.lock");
  }

  async unlock(passphrase: string): Promise<void> {
    await this.bridge.call<Record<string, never>>("wallet.unlock", { passphrase });
  }

  async activity(): Promise<WalletActivity[]> {
    const result = await this.bridge.call<{ items: WalletActivity[] }>("wallet.activity");
    return result.items ?? [];
  }

  /**
   * Ask the device to sign a serialised Solana transaction message.
   *
   * `message` is base64. The private key lives in `solweard` and is never
   * exposed here; the daemon always raises an on-screen confirmation and
   * rejects with error -32002 if the wearer declines.
   */
  async signTransaction(
    message: string,
    options: { encoding?: "base64" | "base58" | "hex"; label?: string } = {},
  ): Promise<string> {
    const appId = this.bridge.current?.appId ?? "unknown.app";
    const result = await this.bridge.call<WalletSignature>("wallet.signTransaction", {
      appId,
      message,
      ...options,
    });
    return result.signature;
  }
}

export class NfcClient {
  constructor(private readonly bridge: Bridge) {}

  status(): Promise<NfcStatus> {
    return this.bridge.call<NfcStatus>("nfc.status");
  }

  async setEnabled(enabled: boolean): Promise<void> {
    await this.bridge.call<Record<string, never>>("nfc.setEnabled", { enabled });
  }

  walletRecord(): Promise<NfcWalletRecord> {
    return this.bridge.call<NfcWalletRecord>("nfc.walletRecord");
  }

  diagnostics(): Promise<NfcDiagnostics> {
    return this.bridge.call<NfcDiagnostics>("nfc.diagnostics");
  }
}
