/** Console output and the error type the CLI uses for anything the user can fix. */

import { stderr, stdout } from "node:process";

const useColour = stdout.isTTY === true && process.env["NO_COLOR"] === undefined;

const wrap = (code: string) => (text: string) =>
  useColour ? `\u001b[${code}m${text}\u001b[0m` : text;

export const colour = {
  bold: wrap("1"),
  dim: wrap("2"),
  red: wrap("31"),
  green: wrap("32"),
  yellow: wrap("33"),
  blue: wrap("34"),
  cyan: wrap("36"),
};

export function info(message: string): void {
  stdout.write(`${message}\n`);
}

export function step(message: string): void {
  stdout.write(`${colour.cyan("-")} ${message}\n`);
}

export function success(message: string): void {
  stdout.write(`${colour.green("ok")} ${message}\n`);
}

export function warn(message: string): void {
  stderr.write(`${colour.yellow("!")} ${message}\n`);
}

export function fail(message: string): void {
  stderr.write(`${colour.red("x")} ${message}\n`);
}

/**
 * An error the user is expected to see and act on. `hint` is printed after the
 * message and should say exactly what to do next: a command to run, a file to
 * edit, a flag to add. Anything thrown that is not a CliError is treated as a
 * bug in the tool and printed with its stack.
 */
export class CliError extends Error {
  readonly hint: string | undefined;
  readonly exitCode: number;

  constructor(message: string, options: { hint?: string; exitCode?: number } = {}) {
    super(message);
    this.name = "CliError";
    this.hint = options.hint;
    this.exitCode = options.exitCode ?? 1;
  }
}

export function reportError(error: unknown): number {
  if (error instanceof CliError) {
    fail(error.message);
    if (error.hint) stderr.write(`\n  ${colour.bold("Try this:")} ${error.hint}\n`);
    return error.exitCode;
  }
  fail("solwear hit an unexpected error. This is a bug in the tool.");
  stderr.write(`\n${error instanceof Error ? (error.stack ?? error.message) : String(error)}\n`);
  return 70;
}
