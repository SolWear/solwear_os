/** `solwear new` — scaffold a project that builds and packages with no edits. */

import { cpSync, existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { basename, join, resolve } from "node:path";
import type { ParsedArgs } from "../args.js";
import { closest, rejectUnknownFlags, stringFlag } from "../args.js";
import { CliError, colour, info, step, success } from "../log.js";
import { templatesDir } from "../paths.js";

export const NEW_FLAGS = ["template", "id", "author", "description", "dir"];

export const TEMPLATES = ["watchface", "app", "signer"] as const;
export type TemplateName = (typeof TEMPLATES)[number];

/** Text files get token substitution; anything else is copied byte for byte. */
const TEXT_EXTENSIONS = new Set([".ts", ".tsx", ".js", ".json", ".html", ".css", ".md", ".txt", ".gitignore"]);

interface Tokens {
  NAME: string;
  ID: string;
  SLUG: string;
  AUTHOR: string;
  DESCRIPTION: string;
  YEAR: string;
}

/** Turn a project name into a filesystem- and manifest-safe slug. */
export function slugify(name: string): string {
  const slug = name
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "");
  if (!slug) throw new CliError(`"${name}" contains no letters or digits to build a name from.`);
  return slug;
}

/** Reverse-DNS id derived from the slug, unless the user supplied one. */
export function defaultId(slug: string, template: TemplateName): string {
  const suffix = slug.replace(/-/g, "");
  return `dev.solwear.${template === "watchface" ? "watchface." : ""}${suffix}`;
}

export function applyTokens(text: string, tokens: Tokens): string {
  return text.replace(/__([A-Z]+)__/g, (match, key: string) =>
    key in tokens ? tokens[key as keyof Tokens] : match,
  );
}

function copyTemplate(from: string, to: string, tokens: Tokens): void {
  mkdirSync(to, { recursive: true });
  for (const name of readdirSync(from)) {
    const source = join(from, name);
    // Templates ship the ignore file as "gitignore" so npm does not strip it.
    const target = join(to, name === "gitignore" ? ".gitignore" : name);
    if (statSync(source).isDirectory()) {
      copyTemplate(source, target, tokens);
      continue;
    }
    const extension = name.includes(".") ? name.slice(name.lastIndexOf(".")) : `.${name}`;
    if (TEXT_EXTENSIONS.has(extension)) {
      writeFileSync(target, applyTokens(readFileSync(source, "utf8"), tokens));
    } else {
      cpSync(source, target);
    }
  }
}

export async function newCommand(args: ParsedArgs): Promise<void> {
  rejectUnknownFlags(args, NEW_FLAGS);

  const name = args.positionals[0];
  if (!name) {
    throw new CliError("`solwear new` needs a project name.", {
      hint: "For example: solwear new my-watchface --template watchface",
    });
  }

  const requested = stringFlag(args, "template", { default: "app" })!;
  if (!(TEMPLATES as readonly string[]).includes(requested)) {
    const suggestion = closest(requested, [...TEMPLATES]);
    throw new CliError(`"${requested}" is not a template.`, {
      hint: suggestion
        ? `Did you mean --template ${suggestion}?`
        : `Available templates: ${TEMPLATES.join(", ")}.`,
    });
  }
  const template = requested as TemplateName;

  const target = resolve(stringFlag(args, "dir") ?? name);
  if (existsSync(target) && readdirSync(target).length > 0) {
    throw new CliError(`${target} already exists and is not empty.`, {
      hint: "Pick another name, or pass --dir to choose a different location.",
    });
  }

  const slug = slugify(basename(target));
  const tokens: Tokens = {
    NAME: name,
    SLUG: slug,
    ID: stringFlag(args, "id") ?? defaultId(slug, template),
    AUTHOR: stringFlag(args, "author") ?? "Unknown",
    DESCRIPTION:
      stringFlag(args, "description") ??
      (template === "watchface" ? `The ${name} watchface.` : `${name}, a SolWear app.`),
    YEAR: String(new Date().getFullYear()),
  };

  const source = join(templatesDir, template);
  if (!existsSync(source)) {
    throw new CliError(`The ${template} template is missing from this installation of solwear.`, {
      hint: "Reinstall the CLI: npm install -g @solwear/cli",
    });
  }

  step(`creating ${colour.bold(tokens.ID)} from the ${template} template`);
  copyTemplate(source, target, tokens);

  success(`created ${target}`);
  info("");
  info("  Next steps:");
  info(`    cd ${basename(target)}`);
  info("    solwear run          # open it in the host emulator");
  info("    solwear package      # produce dist/" + `${tokens.ID}-1.0.0.swa`);
  info("");
}

