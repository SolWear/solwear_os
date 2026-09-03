/**
 * `solwear doctor` — check the toolchain and say exactly how to fix what is
 * missing.
 *
 * The rule this command follows: never report a problem without the command
 * that solves it. A developer running `doctor` is already stuck.
 */

import { execFileSync } from "node:child_process";
import { existsSync } from "node:fs";
import { platform } from "node:os";
import { join } from "node:path";
import type { ParsedArgs } from "../args.js";
import { boolFlag, rejectUnknownFlags } from "../args.js";
import { colour, info, success, warn } from "../log.js";
import { findMonorepoRoot } from "../paths.js";
import { DEFAULT_KEY_PATH } from "./sign.js";

export const DOCTOR_FLAGS = ["json"];

type Status = "ok" | "warn" | "fail";

interface Check {
  name: string;
  status: Status;
  detail: string;
  /** The exact command that fixes it, printed verbatim. */
  fix?: string;
  /** Why this matters, when it is not obvious. */
  because?: string;
}

const isMac = platform() === "darwin";

/** Run a command and return its first line of output, or undefined if absent. */
function probe(command: string, argv: string[]): string | undefined {
  try {
    const output = execFileSync(command, argv, {
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
      timeout: 8000,
    });
    return output.split("\n").find((line) => line.trim() !== "")?.trim();
  } catch {
    return undefined;
  }
}

function which(command: string): string | undefined {
  return probe(isMac || platform() === "linux" ? "which" : "where", [command]);
}

/** The install line for a tool, per platform. Homebrew on macOS, apt on Linux. */
function installCommand(brew: string, apt: string): string {
  return isMac ? `brew install ${brew}` : `sudo apt install ${apt}`;
}

function checkNode(): Check {
  const version = process.versions.node;
  const major = Number(version.split(".")[0]);
  if (major >= 20) return { name: "node", status: "ok", detail: `v${version}` };
  return {
    name: "node",
    status: "fail",
    detail: `v${version} is too old; the CLI and the emulator need Node 20 or newer`,
    fix: isMac ? "brew install node" : "sudo apt install nodejs",
  };
}

function checkRust(): Check {
  const version = probe("cargo", ["--version"]);
  if (version) return { name: "rust", status: "ok", detail: version };
  return {
    name: "rust",
    status: "fail",
    detail: "cargo not found",
    because: "solweard, the system daemon, is written in Rust",
    fix: "curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh",
  };
}

function checkAarch64Target(): Check {
  const targets = probe("rustup", ["target", "list", "--installed"]);
  if (targets === undefined) {
    return {
      name: "rust aarch64 target",
      status: "warn",
      detail: "rustup not found, so the device target cannot be checked",
      fix: "curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh",
    };
  }
  const all = probe("rustup", ["target", "list", "--installed"]) ?? "";
  const installed = all.includes("aarch64-unknown-linux-gnu") || targets.includes("aarch64");
  if (installed) return { name: "rust aarch64 target", status: "ok", detail: "aarch64-unknown-linux-gnu" };
  return {
    name: "rust aarch64 target",
    status: "warn",
    detail: "aarch64-unknown-linux-gnu is not installed",
    because: "needed to cross-compile solweard for the watch, but not to develop apps",
    fix: "rustup target add aarch64-unknown-linux-gnu",
  };
}

function checkQemu(): Check {
  const version = probe("qemu-system-aarch64", ["--version"]);
  if (version) return { name: "qemu", status: "ok", detail: version };
  return {
    name: "qemu",
    status: "warn",
    detail: "qemu-system-aarch64 not found",
    because: "only `solwear run --qemu` needs it; the host emulator does not",
    fix: installCommand("qemu", "qemu-system-arm"),
  };
}

function checkSsh(): Check {
  const version = probe("ssh", ["-V"]) ?? which("ssh");
  if (version) return { name: "ssh", status: "ok", detail: version };
  return {
    name: "ssh",
    status: "fail",
    detail: "ssh not found",
    because: "`solwear install --device` copies packages to a watch over SSH",
    fix: installCommand("openssh", "openssh-client"),
  };
}

function checkBrowser(): Check {
  const candidates = isMac
    ? [
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
        "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge",
        "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser",
      ]
    : ["/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser"];
  const found = candidates.find((path) => existsSync(path)) ?? which("chromium") ?? which("google-chrome");
  if (found) return { name: "chromium", status: "ok", detail: found };
  return {
    name: "chromium",
    status: "warn",
    detail: "no Chromium-based browser found",
    because:
      "the host emulator opens one in app mode to draw the device window; " +
      "without it the emulator falls back to your default browser, which shows tabs and chrome",
    fix: isMac ? "brew install --cask google-chrome" : "sudo apt install chromium",
  };
}

function checkSigningKey(): Check {
  if (existsSync(DEFAULT_KEY_PATH)) {
    return { name: "signing key", status: "ok", detail: DEFAULT_KEY_PATH };
  }
  return {
    name: "signing key",
    status: "warn",
    detail: "no publisher key yet",
    because: "packages must be signed before they can be installed or published",
    fix: `solwear keygen --out ${DEFAULT_KEY_PATH}`,
  };
}

function checkSdkBuild(): Check {
  const root = findMonorepoRoot();
  if (!root) {
    return {
      name: "@solwear/sdk",
      status: "ok",
      detail: "outside the monorepo; the SDK comes from node_modules",
    };
  }
  const runtime = join(root, "sdk", "runtime");
  if (existsSync(join(runtime, "dist", "index.js"))) {
    return { name: "@solwear/sdk", status: "ok", detail: "sdk/runtime/dist is built" };
  }
  return {
    name: "@solwear/sdk",
    status: "fail",
    detail: "sdk/runtime has not been built, so app builds will fail",
    fix: `(cd ${runtime} && npm install && npm run build)`,
  };
}

export function runChecks(): Check[] {
  return [
    checkNode(),
    checkRust(),
    checkAarch64Target(),
    checkQemu(),
    checkSsh(),
    checkBrowser(),
    checkSdkBuild(),
    checkSigningKey(),
  ];
}

const MARK: Record<Status, string> = {
  ok: colour.green("ok  "),
  warn: colour.yellow("warn"),
  fail: colour.red("fail"),
};

export async function doctorCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, DOCTOR_FLAGS);
  const checks = runChecks();

  if (boolFlag(args, "json")) {
    info(JSON.stringify({ checks }, null, 2));
    process.exitCode = checks.some((check) => check.status === "fail") ? 1 : 0;
    return;
  }

  info("");
  for (const check of checks) {
    info(`  ${MARK[check.status]}  ${check.name.padEnd(20)} ${colour.dim(check.detail)}`);
    if (check.status !== "ok") {
      if (check.because) info(`        ${colour.dim(check.because)}`);
      if (check.fix) info(`        ${colour.bold("run:")} ${check.fix}`);
    }
  }
  info("");

  const failures = checks.filter((check) => check.status === "fail");
  const warnings = checks.filter((check) => check.status === "warn");

  if (failures.length === 0 && warnings.length === 0) {
    success("everything the toolchain needs is present");
    return;
  }
  if (failures.length === 0) {
    success(`ready to build apps (${warnings.length} optional ${warnings.length === 1 ? "tool" : "tools"} missing)`);
    return;
  }
  warn(`${failures.length} required ${failures.length === 1 ? "tool is" : "tools are"} missing; run the commands above`);
  process.exitCode = 1;
}
