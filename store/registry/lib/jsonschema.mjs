/**
 * A small JSON Schema validator, covering the subset of draft 2020-12 that the
 * SolWear schemas actually use.
 *
 * Supported keywords:
 *   $ref (local pointers only), type, enum, const,
 *   required, properties, additionalProperties (boolean),
 *   items, minItems, maxItems, uniqueItems,
 *   pattern, minLength, maxLength,
 *   minimum, maximum, multipleOf
 *
 * The registry tooling has to run on a clean clone with nothing installed, so
 * this is deliberately dependency-free. If a schema starts needing a keyword
 * that is not here, add it here rather than adding a dependency — and make the
 * unknown-keyword guard below keep failing loudly, because a silently ignored
 * keyword is a validator that lies.
 */

const SUPPORTED = new Set([
  "$schema",
  "$id",
  "$ref",
  "$defs",
  "title",
  "description",
  "type",
  "enum",
  "const",
  "required",
  "properties",
  "additionalProperties",
  "items",
  "minItems",
  "maxItems",
  "uniqueItems",
  "pattern",
  "minLength",
  "maxLength",
  "minimum",
  "maximum",
  "multipleOf",
]);

function typeOf(value) {
  if (value === null) return "null";
  if (Array.isArray(value)) return "array";
  if (Number.isInteger(value)) return "integer";
  return typeof value;
}

function matchesType(value, expected) {
  const actual = typeOf(value);
  if (expected === "number") return actual === "number" || actual === "integer";
  if (expected === "integer") return actual === "integer";
  return actual === expected;
}

/** Resolve a local `#/a/b` pointer against the root schema. */
function resolveRef(root, ref) {
  if (!ref.startsWith("#/")) {
    throw new Error(`unsupported $ref (local pointers only): ${ref}`);
  }
  let node = root;
  for (const raw of ref.slice(2).split("/")) {
    const key = decodeURIComponent(raw.replace(/~1/g, "/").replace(/~0/g, "~"));
    node = node?.[key];
    if (node === undefined) throw new Error(`$ref does not resolve: ${ref}`);
  }
  return node;
}

function assertKnownKeywords(schema, pointer) {
  for (const key of Object.keys(schema)) {
    if (!SUPPORTED.has(key)) {
      throw new Error(
        `schema at ${pointer || "/"} uses unsupported keyword "${key}"; ` +
          `extend store/registry/lib/jsonschema.mjs before using it`,
      );
    }
  }
}

/**
 * Validate `data` against `schema`.
 * @returns {{path: string, message: string}[]} one entry per problem, empty when valid.
 */
export function validate(data, schema, root = schema, path = "", problems = []) {
  if (typeof schema !== "object" || schema === null) return problems;
  assertKnownKeywords(schema, path);

  if (schema.$ref) {
    return validate(data, resolveRef(root, schema.$ref), root, path, problems);
  }

  const fail = (message) => problems.push({ path: path || "/", message });

  if (schema.type !== undefined) {
    const types = Array.isArray(schema.type) ? schema.type : [schema.type];
    if (!types.some((t) => matchesType(data, t))) {
      fail(`must be ${types.join(" or ")}, got ${typeOf(data)}`);
      return problems; // Further checks would only produce noise.
    }
  }

  if (schema.const !== undefined && JSON.stringify(data) !== JSON.stringify(schema.const)) {
    fail(`must be ${JSON.stringify(schema.const)}`);
  }

  if (schema.enum !== undefined) {
    const ok = schema.enum.some((v) => JSON.stringify(v) === JSON.stringify(data));
    if (!ok) fail(`must be one of ${schema.enum.map((v) => JSON.stringify(v)).join(", ")}`);
  }

  if (typeof data === "string") {
    if (schema.pattern !== undefined && !new RegExp(schema.pattern, "u").test(data)) {
      fail(`must match ${schema.pattern}`);
    }
    if (schema.minLength !== undefined && data.length < schema.minLength) {
      fail(`must be at least ${schema.minLength} characters`);
    }
    if (schema.maxLength !== undefined && data.length > schema.maxLength) {
      fail(`must be at most ${schema.maxLength} characters`);
    }
  }

  if (typeof data === "number") {
    if (schema.minimum !== undefined && data < schema.minimum) fail(`must be >= ${schema.minimum}`);
    if (schema.maximum !== undefined && data > schema.maximum) fail(`must be <= ${schema.maximum}`);
    if (schema.multipleOf !== undefined && data % schema.multipleOf !== 0) {
      fail(`must be a multiple of ${schema.multipleOf}`);
    }
  }

  if (Array.isArray(data)) {
    if (schema.minItems !== undefined && data.length < schema.minItems) {
      fail(`must have at least ${schema.minItems} items`);
    }
    if (schema.maxItems !== undefined && data.length > schema.maxItems) {
      fail(`must have at most ${schema.maxItems} items`);
    }
    if (schema.uniqueItems === true) {
      const seen = new Set();
      for (const item of data) {
        const key = JSON.stringify(item);
        if (seen.has(key)) {
          fail(`items must be unique, ${key} is repeated`);
          break;
        }
        seen.add(key);
      }
    }
    if (schema.items) {
      data.forEach((item, i) => validate(item, schema.items, root, `${path}/${i}`, problems));
    }
  }

  if (typeOf(data) === "object") {
    for (const key of schema.required || []) {
      if (!Object.prototype.hasOwnProperty.call(data, key)) fail(`is missing required "${key}"`);
    }
    const properties = schema.properties || {};
    for (const [key, value] of Object.entries(data)) {
      if (properties[key]) {
        validate(value, properties[key], root, `${path}/${key}`, problems);
      } else if (schema.additionalProperties === false) {
        problems.push({ path: `${path}/${key}`, message: "is not an allowed property" });
      }
    }
  }

  return problems;
}
