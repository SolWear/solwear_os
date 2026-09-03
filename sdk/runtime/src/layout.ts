/**
 * Adaptive layout helpers.
 *
 * Every SolWear surface has to look right from a 240x240 round watch to a
 * 800x480 rectangular panel, so nothing may be sized in constants. These
 * helpers derive a unit scale from the smaller screen dimension and, on round
 * screens, an inset that keeps content out of the clipped corners.
 */

import type { ScreenInfo } from "./protocol.js";

export interface LayoutMetrics {
  width: number;
  height: number;
  shape: ScreenInfo["shape"];
  /** The smaller dimension; the basis for every derived size. */
  base: number;
  /** Padding in pixels that keeps content inside a round bezel. */
  safeInset: number;
  /** Root font size in pixels, so `rem` units scale with the screen. */
  rootFontSize: number;
  /** Convert a fraction of the base dimension into pixels. */
  u: (fraction: number) => number;
}

/**
 * Compute metrics and, when a document is available, publish them as CSS
 * custom properties (`--sw-base`, `--sw-safe`, `--sw-w`, `--sw-h`) plus a
 * `data-shape` attribute on the root element, so stylesheets can adapt without
 * any further JavaScript.
 */
export function layout(screen: ScreenInfo, root?: HTMLElement): LayoutMetrics {
  const width = Math.max(1, Math.round(screen.width));
  const height = Math.max(1, Math.round(screen.height));
  const base = Math.min(width, height);

  // A circle inscribed in the square loses (1 - 1/sqrt(2))/2 of the width at
  // each corner. Half of that is a comfortable, still-generous inset.
  const roundInset = Math.round(base * 0.5 * (1 - Math.SQRT1_2) * 0.85);
  const safeInset = screen.shape === "round" ? roundInset : Math.round(base * 0.04);

  // 16px at a 400px base, floored so tiny screens stay legible.
  const rootFontSize = Math.max(11, Math.round((base / 400) * 16));

  const metrics: LayoutMetrics = {
    width,
    height,
    shape: screen.shape,
    base,
    safeInset,
    rootFontSize,
    u: (fraction: number) => Math.round(base * fraction),
  };

  const element = root ?? (typeof document === "undefined" ? undefined : document.documentElement);
  if (element) {
    element.style.setProperty("--sw-w", `${width}px`);
    element.style.setProperty("--sw-h", `${height}px`);
    element.style.setProperty("--sw-base", `${base}px`);
    element.style.setProperty("--sw-safe", `${safeInset}px`);
    element.style.fontSize = `${rootFontSize}px`;
    element.setAttribute("data-shape", screen.shape);
  }

  return metrics;
}
