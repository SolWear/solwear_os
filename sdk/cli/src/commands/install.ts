/** `solwear install --device <host>` — push a package to a real watch over SSH. */

import { spawn } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { basename, join } from "node:path";
import type { ParsedArgs } from "../args.js";
import { boolFlag, rejectUnknownFlags, stringFlag } from "../args.js";
import { CliError, colour, info, step, success, warn } from "../log.js";
import { packageFileName } from "../manifest.js";
import { findProject } from "../paths.js";
import { sha256Hex, verifyEntries } from "../signing.js";
import { readZip } from "../zip.js";
import { packageProject } from "./package.js";

export const INSTALL_FLAGS = ["device", "package", "user", "port", "identity", "no-build", "allow-unsigned"];

/** Where the daemon looks for packages dropped onto the device. */
const INCOMING_DIR = "/var/lib/solwear/incoming";

interface Ssh {
  host: string;
  user: string;
  port: string | undefined;
  identity: string | undefined;
}

function sshArgs(ssh: Ssh, extra: string[]): string[] {
  const argv: string[] = [];
  if (ssh.port) argv.push("-p", ssh.port);
  if (ssh.identity) argv.push("-i", ssh.identity);
  argv.push("-o", "BatchMode=yes", "-o", "ConnectTimeout=8");
  argv.push(`${ssh.user}@${ssh.host}`, ...extra);
  return argv;
}

function scpArgs(ssh: Ssh, from: string, to: string): string[] {
  const argv: string[] = [];
  // scp spells the port flag with a capital P, unlike ssh.
  if (ssh.port) argv.push("-P", ssh.port);
  if (ssh.identity) argv.push("-i", ssh.identity);
  argv.push("-o", "BatchMode=yes", "-o", "ConnectTimeout=8", from, `${ssh.user}@${ssh.host}:${to}`);
  return argv;
}

interface Executed {
  code: number;
  stdout: string;
  stderr: string;
}

function execute(command: string, argv: string[]): Promise<Executed> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, argv, { stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (chunk: Buffer) => (stdout += chunk.toString()));
    child.stderr.on("data", (chunk: Buffer) => (stderr += chunk.toString()));
    child.on("error", (error) => {
      reject(
        new CliError(`Could not run ${command}: ${error.message}`, {
          hint: "Install OpenSSH, or make sure ssh and scp are on your PATH.",
        }),
      );
    });
    child.on("close", (code) => resolve({ code: code ?? 1, stdout, stderr }));
  });
}

export async function installCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, INSTALL_FLAGS);

  const host = stringFlag(args, "device", {
    required: true,
    hint: "Name the watch: solwear install --device solwear.local",
  })!;
  const ssh: Ssh = {
    host,
    user: stringFlag(args, "user", { default: "solwear" })!,
    port: stringFlag(args, "port"),
    identity: stringFlag(args, "identity"),
  };

  // Find or build the package.
  let packagePath = stringFlag(args, "package");
  if (!packagePath) {
    const project = findProject();
    const candidate = join(project.distDir, packageFileName(project.manifest));
    if (boolFlag(args, "no-build") && existsSync(candidate)) {
      packagePath = candidate;
    } else {
      const built = await packageProject(project, { build: !boolFlag(args, "no-build") });
      packagePath = built.path;
    }
  }
  if (!existsSync(packagePath)) throw new CliError(`No such package: ${packagePath}`);

  // Verify locally before spending time on the network. A watch will refuse an
  // unsigned package anyway, so telling the developer now is kinder.
  const bytes = readFileSync(packagePath);
  const verification = verifyEntries(readZip(bytes));
  if (!verification.ok) {
    if (!boolFlag(args, "allow-unsigned")) {
      throw new CliError(`This package will not install: ${verification.reason}.`, {
        hint:
          "Sign it with `solwear sign --key ~/.solwear/publisher.key.json`, " +
          "or pass --allow-unsigned to sideload it onto a device in developer mode.",
      });
    }
    warn(`sideloading an unsigned package (${verification.reason})`);
  }

  const remotePath = `${INCOMING_DIR}/${basename(packagePath)}`;
  step(`copying ${basename(packagePath)} to ${ssh.user}@${ssh.host}`);

  const mkdir = await execute("ssh", sshArgs(ssh, ["mkdir", "-p", INCOMING_DIR]));
  if (mkdir.code !== 0) {
    throw new CliError(`Cannot reach ${ssh.user}@${ssh.host}: ${mkdir.stderr.trim() || "ssh failed"}`, {
      hint:
        `Check that the watch is on the network and that your key is authorised:\n` +
        `      ssh-copy-id ${ssh.user}@${ssh.host}\n` +
        `    Then try again. Use --user and --port if the watch is not on the defaults.`,
    });
  }

  const copy = await execute("scp", scpArgs(ssh, packagePath, remotePath));
  if (copy.code !== 0) {
    throw new CliError(`Copying the package failed: ${copy.stderr.trim() || "scp failed"}`, {
      hint: `Is ${INCOMING_DIR} writable by ${ssh.user} on the device?`,
    });
  }

  // The daemon owns installation: it verifies the signature again on device
  // and registers the app. Asking it to do the work keeps one code path.
  step("asking solweard to install it");
  const install = await execute("ssh", sshArgs(ssh, ["solweard", "install", remotePath]));
  if (install.code === 0) {
    success(`installed on ${ssh.host}`);
    if (install.stdout.trim()) info(`  ${install.stdout.trim()}`);
    info(`  sha256 ${sha256Hex(bytes)}`);
    return;
  }

  // A device whose daemon predates the install subcommand still picks the file
  // up from the incoming directory, so say so instead of calling it a failure.
  warn(`solweard install returned ${install.code}: ${install.stderr.trim() || "no message"}`);
  info(`  The package is on the device at ${colour.bold(remotePath)}.`);
  info(`  Install it by hand with: ssh ${ssh.user}@${ssh.host} solweard install ${remotePath}`);
  throw new CliError("The daemon did not confirm the install.", {
    hint: `Check the daemon log: ssh ${ssh.user}@${ssh.host} journalctl -u solweard -n 50`,
  });
}
