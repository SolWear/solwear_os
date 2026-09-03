// Adaptive layout, per section 7 of the architecture specification.
//
// Nothing in the shell uses a pixel constant. Every size is derived from the
// screen reported by `system.info`: `--u` is one percent of the smaller screen
// dimension, and all typography and spacing are expressed as multiples of it.
// Round panels additionally get a safe inset so no content lands in a corner
// the glass clips away.

import type { Screen } from "./types.js";

/**
 * Fraction of the smaller dimension kept clear on each edge.
 *
 * On a round panel the usable area is the square inscribed in the circle, so
 * the inset is exactly half of what the circle takes off the width:
 * (1 - 1/sqrt(2)) / 2. Anything smaller pushes the top row, where the status
 * bar lives, outside the glass: at the very top of a circle the chord is
 * narrow, and a uniform rectangular inset does not account for that.
 */
const INSET_ROUND = 0.5 * (1 - Math.SQRT1_2);
const INSET_FLAT = 0.035;

export function applyScreen(screen: Screen): void {
  const width = Math.max(1, Math.round(screen.width));
  const height = Math.max(1, Math.round(screen.height));
  const min = Math.min(width, height);
  const unit = min / 100;
  const inset = min * (screen.shape === "round" ? INSET_ROUND : INSET_FLAT);

  const style = document.documentElement.style;
  style.setProperty("--screen-w", `${width}px`);
  style.setProperty("--screen-h", `${height}px`);
  style.setProperty("--u", `${unit.toFixed(4)}px`);
  style.setProperty("--inset", `${inset.toFixed(2)}px`);
  style.setProperty("--corner", screen.shape === "round" ? "50%" : `${(unit * 6).toFixed(2)}px`);

  document.documentElement.dataset.shape = screen.shape;
  // A wide panel gets a landscape layout; a square or round one stacks.
  document.documentElement.dataset.orientation = width / height >= 1.35 ? "wide" : "compact";

  fitToViewport(width, height);
  window.addEventListener("resize", () => fitToViewport(width, height), { passive: true });
}

/**
 * On the watch the browser viewport is exactly the panel. In the emulator, or
 * in a desktop browser, it can be anything, so the device surface is scaled to
 * fit rather than reflowed: what a developer sees is what the panel shows.
 */
function fitToViewport(width: number, height: number): void {
  const device = document.getElementById("device");
  if (!device) return;
  const scale = Math.min(window.innerWidth / width, window.innerHeight / height, 1);
  device.style.setProperty("--scale", scale.toFixed(4));
}
