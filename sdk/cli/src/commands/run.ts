/** `solwear run` — build the app and open it in an emulator. */

import { spawn } from "node:child_process";
import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import type { ParsedArgs } from "../args.js";
import { boolFlag, closest, rejectUnknownFlags, stringFlag } from "../args.js";
import { CliError, colour, info, step } from "../log.js";
import { findMonorepoRoot, findProject } from "../paths.js";
import { buildProject } from "./build.js";

export const RUN_FLAGS = ["qemu", "profile", "list-profiles", "port", "rpc-port", "no-build", "no-window", "shell", "image"];

/** The emulator lives in the monorepo, not in the published CLI package. */
function emulatorDir(kind: "host" | "qemu"): string {
  const root = findMonorepoRoot();
  if (!root) {
    throw new CliError("The emulator is part of the SolWear monorepo and is not bundled with the CLI.", {
      hint: "Clone the monorepo and run solwear from inside it, or point SOLWEAR_HOME at your checkout.",
    });
  }
  const dir = join(root, "emulator", kind);
  if (!existsSync(dir)) {
    throw new CliError(`${dir} is missing from this checkout.`, { hint: "Try `git pull` in the monorepo." });
  }
  return dir;
}

function profilesDir(): string {
  return join(emulatorDir("host"), "profiles");
}

export function listProfiles(): string[] {
  const dir = profilesDir();
  if (!existsSync(dir)) return [];
  return readdirSync(dir)
    .filter((name) => name.endsWith(".json"))
    .map((name) => name.replace(/\.json$/, ""))
    .sort();
}

function describeProfile(name: string): string {
  try {
    const raw = JSON.parse(readFileSync(join(profilesDir(), `${name}.json`), "utf8")) as {
      label?: string;
      screen?: { width: number; height: number; shape: string };
    };
    const screen = raw.screen;
    return screen ? `${screen.width}x${screen.height} ${screen.shape}` : (raw.label ?? "");
  } catch {
    return "";
  }
}

async function runHost(args: ParsedArgs): Promise<void> {
  const profile = stringFlag(args, "profile", { default: "pi-round-480" })!;
  const available = listProfiles();
  if (available.length > 0 && !available.includes(profile)) {
    const suggestion = closest(profile, available);
    throw new CliError(`"${profile}" is not a device profile.`, {
      hint: suggestion
        ? `Did you mean --profile ${suggestion}?`
        : `Available profiles: ${available.join(", ")}.`,
    });
  }

  const project = findProject();
  if (!boolFlag(args, "no-build")) await buildProject(project, {});

  const emulator = join(emulatorDir("host"), "bin", "solwear-emulator.mjs");
  if (!existsSync(emulator)) {
    throw new CliError(`The host emulator is missing at ${emulator}.`, {
      hint: "Check out the monorepo again, or run `npm install` in emulator/host.",
    });
  }

  const argv = [emulator, "--app", project.distDir, "--profile", profile];
  const port = stringFlag(args, "port");
  if (port) argv.push("--port", port);
  // The emulator binds two ports, and a collision on either one stops it. A
  // developer with a daemon already running needs to be able to move both.
  const rpcPort = stringFlag(args, "rpc-port");
  if (rpcPort) argv.push("--rpc-port", rpcPort);
  const shell = stringFlag(args, "shell");
  if (shell) argv.push("--shell", shell);
  // Useful from a script or over SSH, where there is no display to open a
  // window on: the emulator serves the shell and prints its URL instead.
  if (boolFlag(args, "no-window")) argv.push("--no-window");

  step(`starting the host emulator with ${colour.bold(profile)} (${describeProfile(profile)})`);
  await runProcess(process.execPath, argv, { inherit: true });
}

async function runQemu(args: ParsedArgs): Promise<void> {
  const script = join(emulatorDir("qemu"), "run.sh");
  if (!existsSync(script)) {
    throw new CliError(`The QEMU launcher is missing at ${script}.`);
  }
  const argv: string[] = [];
  const image = stringFlag(args, "image");
  if (image) argv.push("--image", image);
  const port = stringFlag(args, "port");
  if (port) argv.push("--port", port);
  const rpcPort = stringFlag(args, "rpc-port");
  if (rpcPort) argv.push("--rpc-port", rpcPort);

  step("booting the aarch64 image under QEMU (this is the slow path)");
  await runProcess("/bin/sh", [script, ...argv], { inherit: true });
}

export function runProcess(
  command: string,
  argv: string[],
  options: { inherit?: boolean; cwd?: string } = {},
): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, argv, {
      stdio: options.inherit ? "inherit" : "pipe",
      cwd: options.cwd ?? process.cwd(),
    });
    child.on("error", (error) => {
      reject(
        new CliError(`Could not start ${command}: ${error.message}`, {
          hint: command === "ssh" || command === "scp" ? "Is OpenSSH installed and on your PATH?" : undefined,
        }),
      );
    });
    child.on("exit", (code, signal) => {
      // Ctrl+C in the emulator is a normal way to finish, not a failure.
      if (signal === "SIGINT" || signal === "SIGTERM" || code === 0 || code === null) resolve();
      else reject(new CliError(`${command} exited with code ${code}.`));
    });
  });
}

export async function runCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, RUN_FLAGS);

  if (boolFlag(args, "list-profiles")) {
    const profiles = listProfiles();
    if (profiles.length === 0) {
      info("No device profiles found. Are you inside the monorepo?");
      return;
    }
    info("Device profiles:");
    for (const name of profiles) info(`  ${name.padEnd(20)} ${describeProfile(name)}`);
    return;
  }

  if (boolFlag(args, "qemu")) await runQemu(args);
  else await runHost(args);
}
