// Shapes shared across the shell. They mirror the daemon's JSON-RPC results
// exactly; nothing here may drift from docs/ARCHITECTURE.md section 4.2.

export type ScreenShape = "round" | "square" | "rect";

export interface Screen {
  width: number;
  height: number;
  shape: ScreenShape;
}

export interface SystemInfo {
  version: string;
  device: string;
  screen: Screen;
}

export interface PowerStatus {
  percent: number;
  charging: boolean;
  estimateMinutes: number;
}

export interface SensorReading {
  sensor: string;
  value: number;
  unit: string;
  timestampMs: number;
}

export interface NetworkStatus {
  connected: boolean;
  ssid: string | null;
  signal: number | null;
}

export interface AppRecord {
  id: string;
  name: string;
  version: string;
  type: "app" | "watchface";
  entry: string;
  icon?: string;
  capabilities: string[];
  author: string;
  description: string;
  installedAtMs: number;
  signed: boolean;
  publisherKey?: string;
  url: string;
}

export interface NotificationItem {
  id: string;
  title: string;
  body: string;
  appId: string;
  timestampMs: number;
}

export interface ConfirmRequest {
  requestId: string;
  appId: string;
  summary: {
    appId: string;
    byteLength: number;
    encoding: string;
    digest: string;
    publicKey: string;
    label?: string | null;
  };
}
