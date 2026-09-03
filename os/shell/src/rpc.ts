// JSON-RPC 2.0 client over the daemon's localhost WebSocket.
//
// The socket is the shell's only channel to the system, so the client
// reconnects on its own with a bounded backoff and replays nothing: callers
// see a rejected promise and decide whether to retry.

export type Params = Record<string, unknown>;

export interface RpcErrorShape {
  code: number;
  message: string;
  data?: unknown;
}

export class RpcError extends Error {
  readonly code: number;
  readonly data: unknown;

  constructor(error: RpcErrorShape) {
    super(error.message);
    this.name = "RpcError";
    this.code = error.code;
    this.data = error.data;
  }
}

/** Capability denied, as specified in section 4.3. */
export const CAPABILITY_DENIED = -32001;

interface Pending {
  resolve: (value: unknown) => void;
  reject: (error: unknown) => void;
  timer: number;
}

type EventHandler = (params: Record<string, unknown>) => void;
type StatusHandler = (connected: boolean) => void;

const DEFAULT_TIMEOUT_MS = 20_000;
const MAX_BACKOFF_MS = 5_000;

export class RpcClient {
  private socket: WebSocket | null = null;
  private nextId = 1;
  private readonly pending = new Map<number, Pending>();
  private readonly events = new Map<string, Set<EventHandler>>();
  private readonly statusHandlers = new Set<StatusHandler>();
  private backoff = 250;
  private closed = false;

  private url: string;

  constructor(url: string = defaultUrl()) {
    this.url = url;
  }

  /**
   * Point the client at a different socket. The daemon publishes its real
   * address at `/system.json`, which is the only way the shell can know the
   * port when the daemon was not started on the default one. Must be called
   * before `connect`.
   */
  configure(url: string): void {
    if (this.socket) throw new Error("the RPC client is already connected");
    this.url = url;
  }

  get connected(): boolean {
    return this.socket?.readyState === WebSocket.OPEN;
  }

  connect(): void {
    if (this.closed || this.socket) return;

    const socket = new WebSocket(this.url);
    this.socket = socket;

    socket.addEventListener("open", () => {
      this.backoff = 250;
      this.statusHandlers.forEach((handler) => handler(true));
    });

    socket.addEventListener("message", (event) => {
      this.receive(typeof event.data === "string" ? event.data : "");
    });

    const drop = () => {
      if (this.socket !== socket) return;
      this.socket = null;
      this.failAll(new Error("the connection to solweard was lost"));
      this.statusHandlers.forEach((handler) => handler(false));
      if (!this.closed) {
        window.setTimeout(() => this.connect(), this.backoff);
        this.backoff = Math.min(this.backoff * 2, MAX_BACKOFF_MS);
      }
    };
    socket.addEventListener("close", drop);
    socket.addEventListener("error", drop);
  }

  close(): void {
    this.closed = true;
    this.socket?.close();
    this.socket = null;
  }

  /**
   * Invoke a method. `appId` stamps the call on behalf of a sandboxed app;
   * omitting it calls as the shell, which is privileged.
   */
  call<T = unknown>(
    method: string,
    params: Params = {},
    appId?: string,
    timeoutMs = DEFAULT_TIMEOUT_MS,
  ): Promise<T> {
    return new Promise<T>((resolve, reject) => {
      if (!this.connected || !this.socket) {
        reject(new Error("solweard is not connected"));
        return;
      }
      const id = this.nextId++;
      const timer = window.setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`${method} timed out`));
      }, timeoutMs);

      this.pending.set(id, {
        resolve: resolve as (value: unknown) => void,
        reject,
        timer,
      });

      const message: Record<string, unknown> = { jsonrpc: "2.0", id, method, params };
      if (appId) message.appId = appId;
      this.socket.send(JSON.stringify(message));
    });
  }

  /** Subscribe to a pushed event, e.g. `wallet.confirmRequest`. */
  on(method: string, handler: EventHandler): () => void {
    let set = this.events.get(method);
    if (!set) {
      set = new Set();
      this.events.set(method, set);
    }
    set.add(handler);
    return () => set?.delete(handler);
  }

  onStatus(handler: StatusHandler): void {
    this.statusHandlers.add(handler);
  }

  private receive(text: string): void {
    if (!text) return;
    let message: Record<string, unknown>;
    try {
      message = JSON.parse(text) as Record<string, unknown>;
    } catch {
      return;
    }

    // A message without an id is a pushed event.
    if (message.id === undefined || message.id === null) {
      const method = typeof message.method === "string" ? message.method : "";
      const params = (message.params as Record<string, unknown>) ?? {};
      this.events.get(method)?.forEach((handler) => handler(params));
      return;
    }

    const id = Number(message.id);
    const pending = this.pending.get(id);
    if (!pending) return;
    this.pending.delete(id);
    window.clearTimeout(pending.timer);

    if (message.error) {
      pending.reject(new RpcError(message.error as RpcErrorShape));
    } else {
      pending.resolve(message.result);
    }
  }

  private failAll(reason: Error): void {
    for (const pending of this.pending.values()) {
      window.clearTimeout(pending.timer);
      pending.reject(reason);
    }
    this.pending.clear();
  }
}

/** The device ports from section 4 of the architecture specification. */
export function defaultUrl(): string {
  // The shell is served by the same daemon on port 8731, so the RPC socket is
  // the neighbouring port on the same host. This is the answer on a device and
  // the fallback everywhere else; `discoverRpcUrl` gets the authoritative one.
  const host = window.location.hostname || "127.0.0.1";
  return `ws://${host}:8730/rpc`;
}

/**
 * Ask whoever is serving the shell where the JSON-RPC socket is.
 *
 * Both `solweard` and the host emulator answer `/system.json` with an `rpcUrl`.
 * Falling back to the default port keeps the shell working against an older
 * daemon rather than refusing to start.
 */
export async function discoverRpcUrl(): Promise<string> {
  // A full-system emulator serves the shell from inside a guest VM while the
  // browser and WebSocket client live on the host.  The QEMU launcher appends
  // the forwarded socket as `?rpc=...`; accepting it here keeps the on-device
  // discovery path unchanged and also lets several VMs use different ports.
  const forwarded = new URLSearchParams(window.location.search).get("rpc");
  if (forwarded?.startsWith("ws://127.0.0.1:") || forwarded?.startsWith("ws://localhost:")) {
    return forwarded;
  }
  try {
    const response = await fetch("/system.json", { cache: "no-store" });
    if (!response.ok) return defaultUrl();
    const document = (await response.json()) as { rpcUrl?: unknown };
    if (typeof document.rpcUrl === "string" && document.rpcUrl.startsWith("ws")) {
      return document.rpcUrl;
    }
  } catch {
    // No discovery document, or it is not JSON. The default is still right on
    // a device, so this is not worth failing over.
  }
  return defaultUrl();
}
