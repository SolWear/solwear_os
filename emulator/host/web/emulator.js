const state = await fetch("/emulator/state.json").then((response) => response.json());
const device = document.querySelector("#device");
const shell = document.querySelector("#shell");
const select = document.querySelector("#profile");
const status = document.querySelector("#status");
const byId = (id) => document.getElementById(id);
let devPaused = false;
let syncing = false;

function apply(profile) {
  device.style.width = `${profile.screen.width + 36}px`;
  device.style.height = `${profile.screen.height + 36}px`;
  device.className = profile.screen.shape;
  status.textContent = `${profile.screen.width}×${profile.screen.height} · ${state.app.name}`;
}

for (const profile of state.profiles) {
  const option = document.createElement("option");
  option.value = profile.id;
  option.textContent = profile.label ?? `${profile.id} (${profile.screen.width}×${profile.screen.height})`;
  option.selected = profile.id === state.profile.id;
  select.append(option);
}
apply(state.profile);

select.addEventListener("change", async () => {
  const next = await fetch(`/emulator/profile?id=${encodeURIComponent(select.value)}`).then((response) => response.json());
  apply(next.profile);
  shell.contentWindow.location.reload();
});
shell.addEventListener("load", () => shell.contentWindow.focus());
new EventSource("/emulator/reload").addEventListener("message", () => shell.contentWindow.location.reload());

function duration(ms) {
  const seconds = Math.floor(ms / 1000);
  if (seconds < 60) return `${seconds}s`;
  return `${Math.floor(seconds / 60)}m ${seconds % 60}s`;
}

async function control(name, value) {
  await fetch(`/emulator/control?name=${encodeURIComponent(name)}&value=${encodeURIComponent(value)}`);
  await refreshDeveloperTools();
}

function bindRange(id, name) {
  const input = byId(id);
  input.addEventListener("change", () => void control(name, input.value));
}

bindRange("battery-control", "battery");
bindRange("brightness-control", "brightness");
byId("charging-control").addEventListener("change", (event) => void control("charging", event.currentTarget.checked));
byId("nfc-control").addEventListener("change", (event) => void control("nfc", event.currentTarget.checked));
byId("steps-control").addEventListener("change", (event) => void control("steps", event.currentTarget.value));
byId("heart-control").addEventListener("change", (event) => void control("heartRate", event.currentTarget.value));
byId("temp-control").addEventListener("change", (event) => void control("temperature", event.currentTarget.value));
byId("inject-notification").addEventListener("click", () => void control("notification", "Developer ping"));
byId("pause-log").addEventListener("click", (event) => {
  devPaused = !devPaused;
  event.currentTarget.textContent = devPaused ? "Resume" : "Pause";
});

async function refreshDeveloperTools() {
  if (syncing) return;
  syncing = true;
  try {
    const dev = await fetch("/emulator/devtools.json", { cache: "no-store" }).then((response) => response.json());
    byId("dev-live").textContent = "live";
    byId("metric-uptime").textContent = duration(dev.uptimeMs);
    byId("metric-rpc").textContent = dev.rpcCount;
    byId("metric-sockets").textContent = dev.connections;
    byId("metric-errors").textContent = dev.rpcErrors;
    byId("battery-out").textContent = `${dev.hal.power.percent}%`;
    byId("brightness-out").textContent = `${dev.hal.brightness}%`;
    if (document.activeElement?.tagName !== "INPUT") {
      byId("battery-control").value = dev.hal.power.percent;
      byId("charging-control").checked = dev.hal.power.charging;
      byId("nfc-control").checked = dev.nfc.enabled;
      byId("brightness-control").value = dev.hal.brightness;
      byId("steps-control").value = Math.round(dev.hal.sensors.steps.value);
      byId("heart-control").value = Math.round(dev.hal.sensors.heartRate.value);
      byId("temp-control").value = dev.hal.sensors.temperature.value;
    }
    byId("dev-summary").textContent = `${dev.profile} · ${dev.apps} apps · ${dev.notifications} notifications`;
    byId("wallet").textContent = `wallet ${dev.wallet}`;
    byId("wallet").title = dev.wallet;
    byId("nfc-status").textContent = `NFC ${dev.nfc.enabled ? "armed" : "idle"} · ${dev.nfc.backend}`;
    if (!devPaused) {
      byId("rpc-log").replaceChildren(...dev.log.map((entry) => {
        const row = document.createElement("li");
        if (!entry.ok) row.className = "bad";
        const time = document.createElement("time");
        time.textContent = new Date(entry.at).toLocaleTimeString([], { hour12: false, minute: "2-digit", second: "2-digit" });
        const method = document.createElement("span");
        method.textContent = `${entry.caller} › ${entry.method}`;
        const durationView = document.createElement("span");
        durationView.textContent = entry.ok ? `${entry.durationMs}ms` : "error";
        row.title = entry.error ?? "";
        row.append(time, method, durationView);
        return row;
      }));
    }
  } catch {
    byId("dev-live").textContent = "offline";
  } finally {
    syncing = false;
  }
}

void refreshDeveloperTools();
setInterval(() => void refreshDeveloperTools(), 1000);
