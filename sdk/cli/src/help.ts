/** The help text. Kept in one place so `--help` and an unknown command agree. */

import { colour, info } from "./log.js";

export interface CommandHelp {
  usage: string;
  summary: string;
  details?: string[];
}

export const HELP: Record<string, CommandHelp> = {
  new: {
    usage: "solwear new <name> [--template watchface|app|signer]",
    summary: "Create a project that builds and packages with no edits.",
    details: [
      "--template <name>     watchface, app or signer (default: app)",
      "--id <reverse.dns>    app id; derived from the name when omitted",
      "--author <name>       written into manifest.json",
      "--description <text>  one sentence, shown in the store",
      "--dir <path>          create somewhere other than ./<name>",
    ],
  },
  build: {
    usage: "solwear build [--watch] [--minify]",
    summary: "Bundle the TypeScript and stage the package contents in dist/.",
    details: ["--watch    rebuild on every change", "--minify   optimise the bundle for the device"],
  },
  run: {
    usage: "solwear run [--profile <name>] [--qemu]",
    summary: "Build the app and open it in an emulator.",
    details: [
      "--profile <name>   device profile (default: pi-round-480)",
      "--list-profiles    show every profile and its screen",
      "--qemu             boot the real aarch64 image instead; much slower",
      "--port <number>    port for the emulator's HTTP server",
      "--rpc-port <n>     port for the emulator's JSON-RPC socket",
      "--no-window        serve the emulator and print its URL, opening no window",
      "--shell <dir>      serve a specific shell build",
      "--no-build         use the existing dist/ as it is",
    ],
  },
  package: {
    usage: "solwear package [--out <file.swa>]",
    summary: "Produce dist/<id>-<version>.swa from the build output.",
    details: ["--out <file>   write somewhere else", "--no-build     package the existing dist/"],
  },
  sign: {
    usage: "solwear sign --key <path> [--package <file.swa>]",
    summary: "Add signature.json to a package.",
    details: [
      "--key <path>       Ed25519 key (default: ~/.solwear/publisher.key.json)",
      "--package <file>   sign a specific package instead of this project's",
      "--out <file>       write the signed package somewhere else",
    ],
  },
  keygen: {
    usage: "solwear keygen [--out <path>]",
    summary: "Generate an Ed25519 publisher key.",
    details: ["--out <path>   default: ~/.solwear/publisher.key.json", "--force        overwrite an existing key"],
  },
  verify: {
    usage: "solwear verify <file.swa>",
    summary: "Check a package's signature and print what is inside it.",
  },
  install: {
    usage: "solwear install --device <host>",
    summary: "Build, package and push to a watch over SSH.",
    details: [
      "--device <host>     hostname or address of the watch",
      "--user <name>       SSH user (default: solwear)",
      "--port <number>     SSH port",
      "--identity <path>   SSH private key",
      "--allow-unsigned    sideload without a signature, developer mode only",
    ],
  },
  publish: {
    usage: "solwear publish [--url <href>]",
    summary: "Prepare a registry entry for a signed package.",
    details: [
      "--url <href>        where the .swa will be hosted",
      "--registry <path>   registry index to update (default: the monorepo's)",
      "--dry-run           print the entry instead of writing it",
    ],
  },
  doctor: {
    usage: "solwear doctor",
    summary: "Check node, rust, qemu, ssh and keys, and print how to fix what is missing.",
    details: ["--json   machine readable output"],
  },
};

export function printHelp(command?: string): void {
  if (command && HELP[command]) {
    const entry = HELP[command]!;
    info("");
    info(`  ${colour.bold(entry.usage)}`);
    info(`  ${entry.summary}`);
    if (entry.details) {
      info("");
      for (const line of entry.details) info(`    ${line}`);
    }
    info("");
    return;
  }

  info("");
  info(`  ${colour.bold("solwear")} — the SolWear OS developer tool`);
  info("");
  info("  Usage: solwear <command> [options]");
  info("");
  for (const [name, entry] of Object.entries(HELP)) {
    info(`    ${name.padEnd(10)} ${entry.summary}`);
  }
  info("");
  info("  solwear <command> --help   options for one command");
  info("  solwear doctor             check the toolchain");
  info("");
}
