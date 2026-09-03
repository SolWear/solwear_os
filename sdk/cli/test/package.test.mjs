import test from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { createZip, readZip } from "../dist/zip.js";
import { generateKeypair, signEntries, verifyEntries, signingPayload, sha256Hex } from "../dist/signing.js";
import { validateManifest } from "../dist/manifest.js";
import { parseArgs, levenshtein } from "../dist/args.js";
import { slugify, defaultId, applyTokens } from "../dist/commands/new.js";
import { compareVersions, mergeIntoRegistry } from "../dist/commands/publish.js";

const cliRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const bin = join(cliRoot, "dist", "bin.js");

const entries = () => [
  { path: "manifest.json", data: Buffer.from('{"id":"tech.solwear.demo"}') },
  { path: "index.html", data: Buffer.from("<h1>hello</h1>") },
  { path: "assets/icon.txt", data: Buffer.from("x".repeat(5000)) },
];

test("zip round trip preserves paths and bytes", () => {
  const original = entries();
  const restored = readZip(createZip(original));
  assert.equal(restored.length, original.length);
  for (const entry of original) {
    const found = restored.find((candidate) => candidate.path === entry.path);
    assert.ok(found, `${entry.path} survived`);
    assert.deepEqual(found.data, entry.data);
  }
});

test("packaging is deterministic, so the same input hashes the same", () => {
  const a = createZip(entries());
  const b = createZip([...entries()].reverse());
  assert.equal(sha256Hex(a), sha256Hex(b), "entry order must not change the archive");
});

test("a signed package verifies", () => {
  const keypair = generateKeypair();
  const files = entries();
  const document = signEntries(files, keypair);
  const signed = [...files, { path: "signature.json", data: Buffer.from(JSON.stringify(document)) }];
  const result = verifyEntries(readZip(createZip(signed)));
  assert.equal(result.ok, true);
  assert.equal(result.publicKey, keypair.publicKeyBase64);
});

test("a tampered file is rejected", () => {
  const keypair = generateKeypair();
  const files = entries();
  const document = signEntries(files, keypair);
  const tampered = [
    { path: "manifest.json", data: Buffer.from('{"id":"tech.solwear.evil"}') },
    ...files.slice(1),
    { path: "signature.json", data: Buffer.from(JSON.stringify(document)) },
  ];
  const result = verifyEntries(readZip(createZip(tampered)));
  assert.equal(result.ok, false);
  assert.match(result.reason, /modified after signing/);
});

test("an added file is rejected", () => {
  const keypair = generateKeypair();
  const files = entries();
  const document = signEntries(files, keypair);
  const withExtra = [
    ...files,
    { path: "payload.js", data: Buffer.from("steal()") },
    { path: "signature.json", data: Buffer.from(JSON.stringify(document)) },
  ];
  const result = verifyEntries(readZip(createZip(withExtra)));
  assert.equal(result.ok, false);
  assert.match(result.reason, /added after/);
});

test("a removed file is rejected", () => {
  const keypair = generateKeypair();
  const files = entries();
  const document = signEntries(files, keypair);
  const withoutIcon = [
    ...files.filter((entry) => entry.path !== "assets/icon.txt"),
    { path: "signature.json", data: Buffer.from(JSON.stringify(document)) },
  ];
  const result = verifyEntries(readZip(createZip(withoutIcon)));
  assert.equal(result.ok, false);
  assert.match(result.reason, /removed after/);
});

test("a signature from the wrong key is rejected", () => {
  const files = entries();
  const document = signEntries(files, generateKeypair());
  document.publicKey = generateKeypair().publicKeyBase64;
  const swapped = [...files, { path: "signature.json", data: Buffer.from(JSON.stringify(document)) }];
  const result = verifyEntries(readZip(createZip(swapped)));
  assert.equal(result.ok, false);
  assert.match(result.reason, /does not match/);
});

test("a package with no signature is rejected", () => {
  const result = verifyEntries(readZip(createZip(entries())));
  assert.equal(result.ok, false);
  assert.match(result.reason, /no signature\.json/);
});

test("the signing payload is sorted, prefixed and stable", () => {
  const payload = signingPayload({ "b.txt": "22", "a.txt": "11" }).toString("utf8");
  assert.equal(payload, "SolWear .swa signature v1\n11  a.txt\n22  b.txt\n");
});

test("manifest validation names every problem it finds", () => {
  assert.throws(
    () => validateManifest({ id: "NotReverseDns", name: "x", version: "1", sdk: "0.1", type: "widget", capabilities: ["gps"] }, "manifest.json"),
    (error) => {
      assert.match(error.message, /reverse-DNS/);
      assert.match(error.message, /semantic versioning/);
      assert.match(error.message, /"type" must be/);
      assert.match(error.message, /not a capability/);
      return true;
    },
  );
});

test("a valid manifest is accepted and defaults are filled in", () => {
  const manifest = validateManifest(
    { id: "tech.solwear.demo", name: "Demo", version: "1.2.3", sdk: "0.1", type: "app", capabilities: [] },
    "manifest.json",
  );
  assert.equal(manifest.entry, "index.html");
  assert.deepEqual(manifest.capabilities, []);
});

test("argument parsing handles values, equals form and bare flags", () => {
  const args = parseArgs(["run", "--profile", "pi-round-480", "--qemu", "--port=9000", "extra"]);
  assert.equal(args.command, "run");
  assert.deepEqual(args.positionals, ["extra"]);
  assert.equal(args.flags.profile, "pi-round-480");
  assert.equal(args.flags.qemu, true);
  assert.equal(args.flags.port, "9000");
  assert.equal(levenshtein("qmeu", "qemu"), 2);
});

test("project name handling", () => {
  assert.equal(slugify("My Watch Face!"), "my-watch-face");
  assert.equal(defaultId("my-watch-face", "watchface"), "dev.solwear.watchface.mywatchface");
  assert.equal(applyTokens("id=__ID__ name=__NAME__ keep=__NOPE__", { ID: "a", NAME: "b" }), "id=a name=b keep=__NOPE__");
  assert.equal(compareVersions("1.2.0", "1.10.0"), -1);
});

test("registry publishing preserves ordered version history and rejects duplicates", () => {
  const workspace = mkdtempSync(join(tmpdir(), "solwear-registry-"));
  const registry = join(workspace, "index.json");
  const make = (version) => ({ id: "tech.solwear.demo", name: "Demo", version, sdk: "0.1", type: "app", url: `https://example.test/${version}.swa`, sha256: version.padEnd(64, "0"), publisher: "Tests", publisherKey: "dGVzdA==" });
  try {
    writeFileSync(registry, '{"schemaVersion":1,"apps":[]}\n');
    mergeIntoRegistry(registry, make("1.1.0"), false);
    mergeIntoRegistry(registry, make("1.0.0"), false);
    const index = JSON.parse(readFileSync(registry, "utf8"));
    assert.deepEqual(index.apps.map((entry) => entry.version), ["1.0.0", "1.1.0"]);
    assert.equal(index.apps[0].sdk, "0.1");
    assert.throws(() => mergeIntoRegistry(registry, make("1.1.0"), false), /already published/);
  } finally {
    rmSync(workspace, { recursive: true, force: true });
  }
});

test("new, build, package, sign and verify work end to end", () => {
  const workspace = mkdtempSync(join(tmpdir(), "solwear-cli-"));
  try {
    const run = (...argv) => execFileSync(process.execPath, [bin, ...argv], { cwd: workspace, encoding: "utf8" });

    run("new", "round-trip", "--template", "app", "--author", "Tests");
    const project = join(workspace, "round-trip");
    const inProject = (...argv) => execFileSync(process.execPath, [bin, ...argv], { cwd: project, encoding: "utf8" });

    inProject("build");
    inProject("package");
    inProject("keygen", "--out", join(workspace, "key.json"));
    inProject("sign", "--key", join(workspace, "key.json"));

    const output = inProject("verify", join(project, "dist", "dev.solwear.roundtrip-1.0.0.swa"));
    assert.match(output, /signature valid/);

    // The bundle must really contain the app code, not an empty shell.
    const archive = readZip(readFileSync(join(project, "dist", "dev.solwear.roundtrip-1.0.0.swa")));
    const app = archive.find((entry) => entry.path === "app.js");
    assert.ok(app.data.toString("utf8").includes("solwear"), "the SDK was bundled in");
  } finally {
    rmSync(workspace, { recursive: true, force: true });
  }
});

test("an unknown command suggests the closest one and exits non-zero", () => {
  try {
    execFileSync(process.execPath, [bin, "packge"], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
    assert.fail("should have exited non-zero");
  } catch (error) {
    assert.equal(error.status, 1);
    assert.match(error.stderr, /solwear package/);
  }
});

test("signing without a key explains how to make one", () => {
  const workspace = mkdtempSync(join(tmpdir(), "solwear-cli-"));
  try {
    writeFileSync(join(workspace, "empty.swa"), Buffer.alloc(0));
    execFileSync(process.execPath, [bin, "sign", "--key", join(workspace, "missing.json"), "--package", join(workspace, "empty.swa")], {
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
    });
    assert.fail("should have exited non-zero");
  } catch (error) {
    assert.match(error.stderr, /solwear keygen/);
  } finally {
    rmSync(workspace, { recursive: true, force: true });
  }
});
