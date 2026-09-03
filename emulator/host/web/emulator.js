const state = await fetch("/emulator/state.json").then((response) => response.json());
const device = document.querySelector("#device");
const shell = document.querySelector("#shell");
const select = document.querySelector("#profile");
const status = document.querySelector("#status");

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
