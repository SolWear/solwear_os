/**
 * The wire contract between an app running inside the shell's sandboxed iframe
 * and the shell itself.
 *
 * Apps never open the JSON-RPC WebSocket. They post these messages to their
 * parent window; the shell validates the app's capabilities and forwards the
 * call to `solweard` on ws://127.0.0.1:8730. The reply travels back the same
 * way. Keeping the app side to postMessage is what makes the sandbox real: an
 * app has no handle on the socket, so it cannot call a method the shell has
 * not agreed to forward.
 */

/** Marker present on every message in both directions. */
export const BRIDGE_PROTOCOL = "solwear.bridge/1";

/** Numeric marker used by the device shell. Messages carry both markers so
 * 0.1 SDKs can also run against early emulator shells. */
export const BRIDGE_VERSION = 1;

/** Sent by the app as soon as the SDK loads, to ask the shell for its context. */
export interface HelloMessage {
  protocol: typeof BRIDGE_PROTOCOL;
  type: "hello";
  /** SDK version, so the shell can warn about a mismatch. */
  sdk: string;
}

/** The shell's answer to `hello`. Carries everything the SDK needs synchronously. */
export interface InitMessage {
  protocol?: typeof BRIDGE_PROTOCOL;
  solwear?: typeof BRIDGE_VERSION;
  type?: "init";
  kind?: "init";
  appId: string;
  capabilities: Capability[];
  screen: ScreenInfo;
  device?: string;
  osVersion?: string;
  /** True when the app is the currently visible surface. */
  visible?: boolean;
}

/** An app-to-shell JSON-RPC request. */
export interface RpcRequestMessage {
  protocol: typeof BRIDGE_PROTOCOL;
  type: "rpc";
  /** Correlation id chosen by the app side. */
  id: number;
  method: string;
  params: Record<string, unknown>;
}

/** The shell's answer to one `rpc` message. Exactly one of result/error is set. */
export interface RpcResponseMessage {
  protocol?: typeof BRIDGE_PROTOCOL;
  solwear?: typeof BRIDGE_VERSION;
  type?: "rpc-result";
  kind?: "result" | "error";
  id: number;
  result?: unknown;
  error?: { code: number; message: string; data?: unknown };
}

/** A shell-to-app push. */
export interface EventMessage {
  protocol?: typeof BRIDGE_PROTOCOL;
  solwear?: typeof BRIDGE_VERSION;
  type?: "event";
  kind?: "event";
  event: string;
  payload: unknown;
}

/**
 * Raw pointer samples, sent up to the shell.
 *
 * Gesture recognition belongs to the shell, not to each app: a swipe from the
 * bottom edge opens the launcher on every screen, and only the shell can know
 * that. So the app frame reports where a pointer went down and came up, in
 * coordinates normalised to its own viewport, and the shell decides whether
 * that was a tap, a swipe, or a system gesture it should keep for itself.
 */
export interface PointerMessage {
  protocol: typeof BRIDGE_PROTOCOL;
  type: "pointer";
  phase: "down" | "up";
  /** 0 at the left or top edge of the app frame, 1 at the right or bottom. */
  x: number;
  y: number;
  t: number;
}

export type AppToShellMessage = HelloMessage | RpcRequestMessage | PointerMessage;
export type ShellToAppMessage = InitMessage | RpcResponseMessage | EventMessage;

/**
 * Capability names, one per JSON-RPC namespace. `solweard` answers a call whose
 * namespace the app did not declare with JSON-RPC error -32001.
 */
export type Capability =
  | "system"
  | "power"
  | "display"
  | "sensors"
  | "notifications"
  | "apps"
  | "wallet"
  | "nfc";

export const CAPABILITIES: readonly Capability[] = [
  "system",
  "power",
  "display",
  "sensors",
  "notifications",
  "apps",
  "wallet",
  "nfc",
];

/** JSON-RPC error code used when a call falls outside the app's capabilities. */
export const ERR_CAPABILITY_DENIED = -32001;
/** JSON-RPC error code used when the user declines a confirmation prompt. */
export const ERR_USER_REJECTED = -32002;

export type ScreenShape = "round" | "square" | "rect";

export interface ScreenInfo {
  width: number;
  height: number;
  shape: ScreenShape;
}
