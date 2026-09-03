/**
 * __NAME__ — a SolWear app.
 *
 * The pattern here holds for every app: wait for the shell handshake, publish
 * the layout variables, then render from events. `tick` arrives once a second
 * while the app is visible, so there is no need for a timer of your own.
 */

import { layout, solwear } from "@solwear/sdk";

const clock = document.getElementById("clock")!;
const battery = document.getElementById("battery")!;
const steps = document.getElementById("steps")!;

const pad = (value: number) => String(value).padStart(2, "0");

async function main(): Promise<void> {
  await solwear.ready();
  layout(solwear.system.screen);

  solwear.on("tick", (t) => {
    clock.textContent = `${pad(t.hours)}:${pad(t.minutes)}`;
  });

  // Anything that changes slowly is polled, and only while the app is on
  // screen: a background app that keeps polling is a background app that
  // flattens the battery.
  await refresh();
  let timer = window.setInterval(refresh, 15_000);
  solwear.on("visibility", ({ visible }) => {
    window.clearInterval(timer);
    if (visible) {
      void refresh();
      timer = window.setInterval(refresh, 15_000);
    }
  });

  // Adapt again if the emulator switches to a different device profile.
  window.addEventListener("resize", () => layout(solwear.system.screen));
}

async function refresh(): Promise<void> {
  const [power, walked] = await Promise.all([
    solwear.power.status(),
    solwear.sensors.read("steps").catch(() => null),
  ]);
  battery.innerHTML = `battery <strong>${power.percent}%</strong>${power.charging ? " charging" : ""}`;
  steps.innerHTML = walked ? `steps <strong>${Math.round(walked.value)}</strong>` : "steps unavailable";
}

void main().catch((error: unknown) => {
  // Failing silently on a watch is the worst outcome: the wearer sees a frozen
  // screen with no idea why. Always put the reason where it can be read.
  clock.textContent = "!";
  battery.textContent = error instanceof Error ? error.message : String(error);
});
