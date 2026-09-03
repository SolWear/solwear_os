/**
 * __NAME__ — a SolWear watchface.
 *
 * A watchface is always on screen, so the rules are stricter than for an app:
 * repaint only what changed, keep the per-second work to a couple of text
 * nodes, and never run a timer of your own. The `tick` event is the clock.
 */

import { layout, solwear } from "@solwear/sdk";

const hours = document.getElementById("hours")!;
const minutes = document.getElementById("minutes")!;
const colon = document.getElementById("colon")!;
const dateLabel = document.getElementById("date")!;
const fill = document.getElementById("fill")!;
const percent = document.getElementById("percent")!;

const pad = (value: number) => String(value).padStart(2, "0");
let lastMinute = -1;
let lastDay = -1;

async function main(): Promise<void> {
  await solwear.ready();
  layout(solwear.system.screen);
  window.addEventListener("resize", () => layout(solwear.system.screen));

  solwear.on("tick", (t) => {
    colon.classList.toggle("off", t.seconds % 2 === 1);

    // Text nodes are only touched when the value actually changed. Writing the
    // same string every second still costs a layout pass on a Pi.
    if (t.minutes !== lastMinute) {
      lastMinute = t.minutes;
      hours.textContent = pad(t.hours);
      minutes.textContent = pad(t.minutes);
    }

    const now = new Date(t.epochMs);
    if (now.getDate() !== lastDay) {
      lastDay = now.getDate();
      dateLabel.textContent = now
        .toLocaleDateString(undefined, { weekday: "short", day: "numeric", month: "short" })
        .replace(/,/g, "");
    }
  });

  await refreshBattery();
  window.setInterval(refreshBattery, 60_000);
}

async function refreshBattery(): Promise<void> {
  try {
    const power = await solwear.power.status();
    fill.style.width = `${Math.max(2, power.percent)}%`;
    fill.classList.toggle("low", power.percent <= 15 && !power.charging);
    percent.textContent = power.charging ? `${power.percent}% +` : `${power.percent}%`;
  } catch {
    // A watchface must keep showing the time even if the daemon is briefly
    // unreachable, so a failed battery read is shown and then forgotten.
    percent.textContent = "--%";
  }
}

void main().catch((error: unknown) => {
  dateLabel.textContent = error instanceof Error ? error.message : String(error);
});
