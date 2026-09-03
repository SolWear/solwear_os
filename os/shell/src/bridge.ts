// The app sandbox.
//
// Every app runs in an iframe with `sandbox="allow-scripts"`, which gives it an
// opaque origin: it cannot reach the shell's DOM, its storage, or another app's
// frame. Its only channel is postMessage, and the shell is the one that stamps
// the app id on the resulting JSON-RPC call. Because the stamp is derived from
// which frame the message arrived in — matched against the live
// `contentWindow` — an app cannot claim to be a different app.

import { CAPABILITY_DENIED, RpcClient, RpcError } from "./rpc.js";
import type { AppRecord, Screen } from "./types.js";

export type AppEvent = "tick" | "visibility" | "button" | "gesture";

interface BridgeMessage {
  solwear?: number;
  kind?: string;
  type?: string;
  id?: number;
  method?: string;
  params?: Record<string, unknown>;
  phase?: "down" | "up";
  x?: number;
  y?: number;
  t?: number;
}

/** Wallet prompts can sit on screen for a while, so they get their own budget. */
const WALLET_TIMEOUT_MS = 120_000;

export class AppHost {
  private frame: HTMLIFrameElement | null = null;
  private record: AppRecord | null = null;
  private ticker = 0;

  constructor(
    private readonly rpc: RpcClient,
    private readonly screen: () => Screen,
    private readonly pointer: (phase: "down" | "up", x: number, y: number, t: number) => void,
  ) {
    window.addEventListener("message", (event) => this.onMessage(event));
  }

  get current(): AppRecord | null {
    return this.record;
  }

  /** Mount an app into `container` and start its event stream. */
  mount(container: HTMLElement, app: AppRecord): HTMLIFrameElement {
    this.unmount();
    const frame = document.createElement("iframe");
    frame.className = "app-frame";
    frame.setAttribute("sandbox", "allow-scripts");
    frame.setAttribute("referrerpolicy", "no-referrer");
    frame.setAttribute("title", app.name);
    frame.src = app.url;
    container.append(frame);

    this.frame = frame;
    this.record = app;

    frame.addEventListener("load", () => {
      this.post({
        kind: "init",
        appId: app.id,
        capabilities: app.capabilities,
        screen: this.screen(),
      });
      this.emit("visibility", { visible: true });
    });

    this.ticker = window.setInterval(() => {
      this.emit("tick", { epochMs: Date.now() });
    }, 1000);

    return frame;
  }

  unmount(): void {
    if (this.ticker) {
      window.clearInterval(this.ticker);
      this.ticker = 0;
    }
    if (this.frame) {
      this.emit("visibility", { visible: false });
      this.frame.remove();
      this.frame = null;
    }
    this.record = null;
  }

  /** Forward a system event into the running app. */
  emit(event: AppEvent, payload: Record<string, unknown>): void {
    this.post({ kind: "event", event, payload });
  }

  private post(message: Record<string, unknown>): void {
    // The frame has an opaque origin, so "*" is the only usable target. That is
    // safe here: the payload carries no secrets, and the frame is one we made.
    this.frame?.contentWindow?.postMessage({ solwear: 1, ...message }, "*");
  }

  private async onMessage(event: MessageEvent): Promise<void> {
    const frame = this.frame;
    const record = this.record;
    if (!frame || !record) return;
    // Identity comes from the frame the message arrived in, never from its
    // contents. This is the whole point of the broker.
    if (event.source !== frame.contentWindow) return;

    const message = event.data as BridgeMessage;
    if (!message || message.solwear !== 1) return;
    const kind = message.kind ?? message.type;
    if (kind === "pointer") {
      if (
        (message.phase === "down" || message.phase === "up") &&
        Number.isFinite(message.x) &&
        Number.isFinite(message.y) &&
        Number.isFinite(message.t)
      ) {
        this.pointer(message.phase, Number(message.x), Number(message.y), Number(message.t));
      }
      return;
    }
    if (kind !== "rpc") return;
    const id = typeof message.id === "number" ? message.id : -1;
    const method = typeof message.method === "string" ? message.method : "";
    const params = message.params ?? {};

    const capability = method.split(".")[0] ?? "";
    if (!record.capabilities.includes(capability)) {
      // Refuse locally as well as in the daemon: an app should get the same
      // answer whether or not the socket happens to be up.
      this.post({
        kind: "error",
        id,
        error: {
          code: CAPABILITY_DENIED,
          message: `app \`${record.id}\` does not hold the \`${capability}\` capability`,
        },
      });
      return;
    }

    try {
      const timeout = method === "wallet.signTransaction" ? WALLET_TIMEOUT_MS : undefined;
      const result = await this.rpc.call(
        method,
        { ...params, appId: record.id },
        record.id,
        timeout,
      );
      this.post({ kind: "result", id, result });
    } catch (error) {
      const shaped =
        error instanceof RpcError
          ? { code: error.code, message: error.message, data: error.data }
          : { code: -32603, message: error instanceof Error ? error.message : String(error) };
      this.post({ kind: "error", id, error: shaped });
    }
  }
}
