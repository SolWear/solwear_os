/** Loading and validating manifest.json, with errors that name the offending field. */

import { readFileSync } from "node:fs";
import { CliError } from "./log.js";

export const CAPABILITIES = [
  "system",
  "power",
  "display",
  "sensors",
  "notifications",
  "apps",
  "wallet",
] as const;

export type Capability = (typeof CAPABILITIES)[number];

export interface Manifest {
  id: string;
  name: string;
  version: string;
  sdk: string;
  type: "app" | "watchface";
  entry: string;
  icon?: string;
  capabilities: Capability[];
  author?: string;
  description?: string;
}

const ID_PATTERN = /^[a-z][a-z0-9]*(\.[a-z0-9][a-z0-9-]*)+$/;
const VERSION_PATTERN = /^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$/;

export function parseManifest(text: string, source: string): Manifest {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch (error) {
    throw new CliError(`${source} is not valid JSON: ${(error as Error).message}`, {
      hint: "Open the file and look for a trailing comma or an unquoted key.",
    });
  }
  return validateManifest(value, source);
}

export function validateManifest(value: unknown, source: string): Manifest {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new CliError(`${source} must contain a JSON object.`);
  }
  const raw = value as Record<string, unknown>;
  const problems: string[] = [];

  const requireString = (field: string): string => {
    const found = raw[field];
    if (typeof found !== "string" || found.trim() === "") {
      problems.push(`"${field}" is required and must be a non-empty string`);
      return "";
    }
    return found;
  };

  const id = requireString("id");
  if (id && !ID_PATTERN.test(id)) {
    problems.push(`"id" must be reverse-DNS, for example tech.solwear.watchface (got "${id}")`);
  }

  const name = requireString("name");
  const version = requireString("version");
  if (version && !VERSION_PATTERN.test(version)) {
    problems.push(`"version" must be semantic versioning, for example 1.0.0 (got "${version}")`);
  }

  const sdk = typeof raw["sdk"] === "string" ? (raw["sdk"] as string) : "";
  if (!sdk) problems.push('"sdk" is required, for example "0.1"');

  const type = raw["type"];
  if (type !== "app" && type !== "watchface") {
    problems.push(`"type" must be either "app" or "watchface" (got ${JSON.stringify(type)})`);
  }

  const entry = typeof raw["entry"] === "string" && raw["entry"] ? (raw["entry"] as string) : "index.html";
  if (entry.startsWith("/") || entry.includes("..")) {
    problems.push(`"entry" must be a relative path inside the package (got "${entry}")`);
  }

  const capabilitiesRaw = raw["capabilities"];
  const capabilities: Capability[] = [];
  if (!Array.isArray(capabilitiesRaw)) {
    problems.push('"capabilities" is required and must be an array, use [] if the app needs nothing');
  } else {
    for (const capability of capabilitiesRaw) {
      if (typeof capability !== "string" || !(CAPABILITIES as readonly string[]).includes(capability)) {
        problems.push(
          `"${String(capability)}" is not a capability. Valid values: ${CAPABILITIES.join(", ")}`,
        );
      } else if (capabilities.includes(capability as Capability)) {
        problems.push(`"${capability}" is listed twice in "capabilities"`);
      } else {
        capabilities.push(capability as Capability);
      }
    }
  }

  const icon = typeof raw["icon"] === "string" ? (raw["icon"] as string) : undefined;
  if (icon && (icon.startsWith("/") || icon.includes(".."))) {
    problems.push(`"icon" must be a relative path inside the package (got "${icon}")`);
  }

  if (problems.length > 0) {
    throw new CliError(
      `${source} is not a valid SolWear manifest:\n` + problems.map((p) => `    - ${p}`).join("\n"),
      { hint: "Section 5 of docs/ARCHITECTURE.md describes every field." },
    );
  }

  const manifest: Manifest = {
    id,
    name,
    version,
    sdk,
    type: type as "app" | "watchface",
    entry,
    capabilities,
  };
  if (icon) manifest.icon = icon;
  if (typeof raw["author"] === "string") manifest.author = raw["author"] as string;
  if (typeof raw["description"] === "string") manifest.description = raw["description"] as string;
  return manifest;
}

export function readManifest(path: string): Manifest {
  let text: string;
  try {
    text = readFileSync(path, "utf8");
  } catch {
    throw new CliError(`No manifest.json at ${path}.`, {
      hint: "Run this command from an app directory, or create one with `solwear new <name>`.",
    });
  }
  return parseManifest(text, path);
}

/** The file name a package gets: `<id>-<version>.swa`. */
export function packageFileName(manifest: Manifest): string {
  return `${manifest.id}-${manifest.version}.swa`;
}
