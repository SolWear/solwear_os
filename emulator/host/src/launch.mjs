/**
 * Opening the emulator window.
 *
 * Electron was the obvious choice and was rejected: it is a 200 MB download and
 * roughly a second of startup before any of our code runs, which does not fit
 * the "under two seconds, from cold" requirement. A Chromium-based browser in
 * app mode gives the same thing — a chrome-less window rendering our page —
 * with nothing to install and a much faster start. If no such browser is
 * present the emulator falls back to the default browser and says so, because a
 * tab with a URL bar is worse but still usable.
 */

import { spawn } from "node:child_process";
import { existsSync, mkdirSync } from "node:fs";
import { homedir, platform } from "node:os";
import { join } from "node:path";

const MAC_BROWSERS = [
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/Applications/Chromium.app/Contents/MacOS/Chromium",
  "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
  "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
  "/Applications/Arc.app/Contents/MacOS/Arc",
];

const LINUX_BROWSERS = [
  "/usr/bin/google-chrome",
  "/usr/bin/google-chrome-stable",
  "/usr/bin/chromium",
  "/usr/bin/chromium-browser",
  "/usr/bin/microsoft-edge",
];

export function findBrowser() {
  const candidates = platform() === "darwin" ? MAC_BROWSERS : LINUX_BROWSERS;
  return candidates.find((path) => existsSync(path));
}

/**
 * Launch the window.
 * @returns {{ child: import("node:child_process").ChildProcess | null, mode: "app" | "fallback" }}
 */
export function openWindow(url, { width, height, browser = findBrowser() } = {}) {
  if (!browser) {
    const opener = platform() === "darwin" ? "open" : "xdg-open";
    const child = spawn(opener, [url], { stdio: "ignore", detached: true });
    child.unref();
    return { child: null, mode: "fallback" };
  }

  // A dedicated profile directory keeps the emulator out of the developer's
  // own browser session, and reusing the same one keeps later starts fast.
  const profileDir = join(homedir(), ".solwear", "emulator-browser");
  mkdirSync(profileDir, { recursive: true });

  const child = spawn(
    browser,
    [
      `--app=${url}`,
      `--user-data-dir=${profileDir}`,
      `--window-size=${Math.round(width)},${Math.round(height)}`,
      "--no-first-run",
      "--no-default-browser-check",
      "--disable-features=Translate,MediaRouter",
      // The emulator page talks to 127.0.0.1 on two ports; nothing here is
      // remote, and the background throttling would stall the tick event.
      "--disable-background-timer-throttling",
      "--disable-backgrounding-occluded-windows",
      "--disable-renderer-backgrounding",
    ],
    { stdio: "ignore" },
  );

  return { child, mode: "app" };
}
