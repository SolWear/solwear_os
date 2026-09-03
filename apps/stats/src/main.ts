/**
 * Stats: health readings and Linux runtime figures.
 *
 * The important rule here is that a missing reading is normal, not an error. A
 * Raspberry Pi 4 has a CPU temperature but no step counter and no battery
 * gauge, and a watch built on a different board will have its own gaps. Each
 * value is therefore fetched and rendered independently: one unavailable sensor
 * blanks its own tile and nothing else. Fetching them together and rendering
 * only on success means the first missing sensor blanks the whole screen, which
 * is exactly what happened on real hardware.
 */

import { layout, solwear, type SystemStats } from "@solwear/sdk";

const view = (id: string) => document.getElementById(id)!;

const UNAVAILABLE = "n/a";

function megabytes(value: number): string {
  if (!value) return UNAVAILABLE;
  const megabytes = value / 1024 / 1024;
  return megabytes >= 1024 ? `${(megabytes / 1024).toFixed(1)} GB` : `${Math.round(megabytes)} MB`;
}

function duration(ms: number): string {
  const hours = Math.floor(ms / 3_600_000);
  const minutes = Math.floor(ms / 60_000) % 60;
  return hours ? `${hours}h ${minutes}m` : `${minutes}m`;
}

/** Run one reading, and turn a failure into a blank rather than a thrown page. */
async function settle<T>(work: Promise<T>): Promise<T | null> {
  try {
    return await work;
  } catch {
    return null;
  }
}

async function renderSteps(): Promise<void> {
  const steps = await settle(solwear.sensors.read("steps"));
  const bar = view("step-bar") as HTMLElement;
  if (!steps) {
    view("steps").textContent = UNAVAILABLE;
    bar.style.width = "0%";
    return;
  }
  view("steps").textContent = Math.round(steps.value).toLocaleString();
  bar.style.width = `${Math.min(100, steps.value / 100)}%`;
}

async function renderSensor(id: string, sensor: string, format: (value: number) => string): Promise<void> {
  const reading = await settle(solwear.sensors.read(sensor));
  view(id).textContent = reading ? format(reading.value) : UNAVAILABLE;
}

async function renderPower(): Promise<void> {
  const power = await settle(solwear.power.status());
  view("battery").textContent = power ? `${power.percent}%${power.charging ? " ⚡" : ""}` : UNAVAILABLE;
}

async function renderSystem(): Promise<void> {
  const stats = await settle(solwear.system.stats());
  if (!stats) {
    view("status").textContent = "System statistics are unavailable";
    return;
  }
  const system: SystemStats = stats;
  view("platform").textContent = `${system.platform.os}/${system.platform.arch}`;
  view("uptime").textContent = duration(system.uptimeMs);
  view("memory").textContent = system.memory.totalBytes
    ? `${megabytes(system.memory.totalBytes - system.memory.availableBytes)} / ${megabytes(system.memory.totalBytes)}`
    : megabytes(system.memory.processBytes);
  view("storage").textContent = system.storage.totalBytes
    ? `${megabytes(system.storage.totalBytes - system.storage.availableBytes)} / ${megabytes(system.storage.totalBytes)}`
    : UNAVAILABLE;
  view("load").textContent = system.load.one.toFixed(2);
  view("apps").textContent = String(system.apps);
  view("status").textContent = system.shellConnected ? "Live via capability-gated RPC" : "Shell disconnected";
}

async function refresh(): Promise<void> {
  await Promise.all([
    renderSteps(),
    renderSensor("heart", "heartRate", (value) => `${Math.round(value)} bpm`),
    renderSensor("temp", "temperature", (value) => `${value.toFixed(1)}°C`),
    renderPower(),
    renderSystem(),
  ]);
}

async function start(): Promise<void> {
  await solwear.ready();
  layout(solwear.system.screen);
  await refresh();
  setInterval(() => void refresh(), 3000);
}

void start().catch((error) => {
  view("status").textContent = String(error);
});
