/**
 * A small argument parser. The CLI has a fixed, well known set of flags, so a
 * dependency-free parser that reports unknown flags precisely beats a generic
 * library here.
 */

import { CliError } from "./log.js";

export interface ParsedArgs {
  command: string | undefined;
  positionals: string[];
  flags: Record<string, string | true>;
}

export function parseArgs(argv: string[]): ParsedArgs {
  const positionals: string[] = [];
  const flags: Record<string, string | true> = {};

  for (let i = 0; i < argv.length; i++) {
    const token = argv[i]!;
    if (token === "--") {
      positionals.push(...argv.slice(i + 1));
      break;
    }
    if (token.startsWith("--")) {
      const body = token.slice(2);
      const eq = body.indexOf("=");
      if (eq >= 0) {
        flags[body.slice(0, eq)] = body.slice(eq + 1);
        continue;
      }
      const next = argv[i + 1];
      if (next !== undefined && !next.startsWith("-")) {
        flags[body] = next;
        i++;
      } else {
        flags[body] = true;
      }
      continue;
    }
    if (token.startsWith("-") && token.length > 1) {
      const short = token.slice(1);
      const next = argv[i + 1];
      if (next !== undefined && !next.startsWith("-")) {
        flags[short] = next;
        i++;
      } else {
        flags[short] = true;
      }
      continue;
    }
    positionals.push(token);
  }

  return { command: positionals.shift(), positionals, flags };
}

/** Read a flag that must carry a value. */
export function stringFlag(
  args: ParsedArgs,
  name: string,
  options: { required?: boolean; default?: string; hint?: string } = {},
): string | undefined {
  const value = args.flags[name];
  if (value === undefined) {
    if (options.required) throw new CliError(`--${name} is required.`, { hint: options.hint });
    return options.default;
  }
  if (value === true) {
    throw new CliError(`--${name} needs a value.`, {
      hint: options.hint ?? `Write it as --${name} <value>.`,
    });
  }
  return value;
}

export function boolFlag(args: ParsedArgs, name: string): boolean {
  const value = args.flags[name];
  return value === true || value === "true";
}

/** Reject flags the command does not understand, and suggest the closest match. */
export function rejectUnknownFlags(args: ParsedArgs, known: string[]): void {
  const allowed = new Set([...known, "help", "h", "verbose"]);
  for (const name of Object.keys(args.flags)) {
    if (allowed.has(name)) continue;
    const suggestion = closest(name, known);
    throw new CliError(`Unknown option --${name}.`, {
      hint: suggestion
        ? `Did you mean --${suggestion}? Run the command with --help to see every option.`
        : `Known options: ${known.map((k) => `--${k}`).join(", ") || "(none)"}.`,
    });
  }
}

export function closest(input: string, candidates: string[]): string | undefined {
  let best: string | undefined;
  let bestDistance = Infinity;
  for (const candidate of candidates) {
    const distance = levenshtein(input, candidate);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = candidate;
    }
  }
  return bestDistance <= 2 ? best : undefined;
}

export function levenshtein(a: string, b: string): number {
  const cols = b.length + 1;
  let previous = Array.from({ length: cols }, (_, i) => i);
  for (let i = 1; i <= a.length; i++) {
    const current = [i];
    for (let j = 1; j < cols; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      current[j] = Math.min(current[j - 1]! + 1, previous[j]! + 1, previous[j - 1]! + cost);
    }
    previous = current;
  }
  return previous[cols - 1]!;
}
