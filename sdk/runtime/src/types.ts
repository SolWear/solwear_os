/** Result and parameter shapes for the JSON-RPC surface in section 4.2 of the spec. */

import type { Capability, ScreenInfo, ScreenShape } from "./protocol.js";

export type { Capability, ScreenInfo, ScreenShape };

export interface SystemInfo {
  version: string;
  device: string;
  screen: ScreenInfo;
}

export interface SystemTime {
  epochMs: number;
  timezone: string;
}

export interface SystemStats {
  uptimeMs: number;
  platform: { os: string; arch: string };
  memory: { totalBytes: number; availableBytes: number; processBytes: number };
  storage: { totalBytes: number; availableBytes: number };
  load: { one: number; five: number; fifteen: number };
  apps: number;
  notifications: number;
  shellConnected: boolean;
}

export interface PowerStatus {
  percent: number;
  charging: boolean;
  /** Minutes of runtime left, or minutes to full when charging. */
  estimateMinutes: number;
}

/** Sensors every HAL implementation must answer, mock included. */
export type SensorName =
  | "heartRate"
  | "steps"
  | "accelerometer"
  | "temperature"
  | "ambientLight";

export interface SensorReading {
  sensor: string;
  value: number;
  unit: string;
  timestampMs: number;
}

export interface Notification {
  id: string;
  title: string;
  body: string;
  appId: string;
  timestampMs: number;
  read?: boolean;
}

export interface AppRecord {
  id: string;
  name: string;
  version: string;
  type: "app" | "watchface";
  icon?: string;
  capabilities: Capability[];
  author?: string;
  description?: string;
}

export interface WalletPublicKey {
  publicKey: string;
}

export interface WalletSignature {
  signature: string;
}

export interface WalletStatus {
  onboarded: boolean;
  locked: boolean;
  protected: boolean;
  name: string;
  publicKey: string;
}

export interface WalletActivity {
  id: string;
  appId: string;
  label: string;
  digest: string;
  byteLength: number;
  timestampMs: number;
}

export interface NfcStatus {
  available: boolean;
  ready: boolean;
  enabled: boolean;
  backend: string;
  mode: string;
  detail?: string;
}

export interface NfcWalletRecord {
  externalType: "solwear:wallet";
  payload: { version: 1; pubkey: string; network: string };
}

export interface NfcDiagnostics {
  status: NfcStatus;
  expectedDevice: string;
  address: string;
  protocol: string;
}

/** Emitted once a second while the app is the visible surface. */
export interface TickEvent {
  epochMs: number;
  /** Local time fields, precomputed so watchfaces do no date maths in the hot path. */
  hours: number;
  minutes: number;
  seconds: number;
}

export interface VisibilityEvent {
  visible: boolean;
}

/** Hardware buttons. `side` is the crown/side button on a round watch. */
export interface ButtonEvent {
  button: "back" | "select" | "side" | "up" | "down";
  action: "down" | "up" | "press" | "longpress";
}

export interface GestureEvent {
  gesture: "swipe-left" | "swipe-right" | "swipe-up" | "swipe-down" | "tap" | "doubletap" | "longpress";
  x: number;
  y: number;
}

export interface SolwearEvents {
  tick: TickEvent;
  visibility: VisibilityEvent;
  button: ButtonEvent;
  gesture: GestureEvent;
}

export type EventName = keyof SolwearEvents;
