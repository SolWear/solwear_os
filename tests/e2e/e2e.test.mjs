/**
 * The SolWear end-to-end test.
 *
 * One run exercises the whole stack: the real `solweard` binary against the
 * mock HAL, real `.swa` packages produced and signed by the real CLI, the real
 * JSON-RPC WebSocket, and the real static asset server. It needs no hardware,
 * no QEMU and no network, and it binds ephemeral ports so it can run while a
 * daemon or an emulator is already up.
 *
 * The negative cases matter as much as the positive one. A package that fails
 * any integrity check must not install, and an app must not be able to reach
 * past its manifest or claim another app's identity.
 *
 * Run it with: tests/e2e/run.sh
 */

import assert from "node:assert/strict";
import { createPublicKey, createHash, verify as verifySignature } from "node:crypto";
import { copyFileSync, readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";
import { after, before, describe, it } from "node:test";

import { createZip, readZip } from "../../sdk/cli/dist/zip.js";
import {
  Daemon,
  RpcConnection,
  base58Decode,
  delay,
  makeTempDir,
  removeTempDir,
  repoRoot,
  requirePrerequisites,
  run,
  settledWithin,
  solwear,
} from "./harness.mjs";

/** DER prefix for an Ed25519 SubjectPublicKeyInfo, so Node can import raw keys. */
const SPKI_PREFIX = Buffer.from("302a300506032b6570032100", "hex");

const APP_ID = "tech.solwear.e2e";
const OTHER_APP_ID = "tech.solwear.e2e-other";

/** State shared by every test, built once in `before`. */
const world = {};

/** Rebuild an archive from its entries, with one file replaced. */
function repack(swaPath, path, data) {
  const entries = readZip(readFileSync(swaPath));
  const index = entries.findIndex((entry) => entry.path === path);
  assert.notEqual(index, -1, `${path} is not in ${swaPath}`);
  entries[index] = { path, data: Buffer.from(data) };
  return createZip(entries);
}

function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

/**
 * Create an app project, build it with the CLI, package it and optionally sign
 * it. Returns the path to the .swa and its whole-archive digest.
 */
async function buildPackage({ id, name, capabilities, key, out }) {
  const projectDir = join(world.work, id);
  await solwear(["new", name, "--template", "app", "--id", id, "--dir", projectDir]);

  // The template grants what a generic app needs; these tests need a specific
  // set, so the manifest is edited exactly as a developer would edit it.
  const manifestPath = join(projectDir, "manifest.json");
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
  manifest.capabilities = capabilities;
  writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);

  await solwear(["package"], { cwd: projectDir });
  const built = join(projectDir, "dist", `${id}-${manifest.version}.swa`);
  copyFileSync(built, out);
  if (key) await solwear(["sign", "--key", key, "--package", out]);
  return { path: out, sha256: sha256(readFileSync(out)), version: manifest.version };
}

before(async () => {
  requirePrerequisites();
  world.work = makeTempDir("e2e-work");
  world.data = makeTempDir("e2e-data");
  world.packages = join(world.work, "packages");
  mkdirSync(world.packages, { recursive: true });

  // Two identities: the publisher the registry would list, and an impostor
  // whose signature is valid but whose key is the wrong one.
  world.publisherKeyPath = join(world.work, "publisher.key.json");
  world.impostorKeyPath = join(world.work, "impostor.key.json");
  await solwear(["keygen", "--out", world.publisherKeyPath]);
  await solwear(["keygen", "--out", world.impostorKeyPath]);
  world.publisherKey = JSON.parse(readFileSync(world.publisherKeyPath, "utf8")).publicKey;
  world.impostorKey = JSON.parse(readFileSync(world.impostorKeyPath, "utf8")).publicKey;

  world.signed = await buildPackage({
    id: APP_ID,
    name: "e2e-app",
    capabilities: ["system", "wallet"],
    key: world.publisherKeyPath,
    out: join(world.packages, `${APP_ID}-1.0.0.swa`),
  });
  world.unsigned = await buildPackage({
    id: OTHER_APP_ID,
    name: "e2e-other",
    capabilities: ["system"],
    out: join(world.packages, `${OTHER_APP_ID}-1.0.0.swa`),
  });
  world.impostorSigned = await buildPackage({
    id: "tech.solwear.e2e-impostor",
    name: "e2e-impostor",
    capabilities: ["system"],
    key: world.impostorKeyPath,
    out: join(world.packages, "tech.solwear.e2e-impostor-1.0.0.swa"),
  });

  world.daemon = new Daemon(world.data);
  await world.daemon.start();
  world.shell = await RpcConnection.open(world.daemon.rpcUrl);
  await world.shell.call("shell.ready");
});

after(async () => {
  world.shell?.close();
  world.app?.close();
  await world.daemon?.stop();
  removeTempDir(world.work);
  removeTempDir(world.data);
});

describe("the daemon comes up on ephemeral ports", () => {
  it("publishes where it bound", () => {
    const runtime = world.daemon.runtime;
    assert.match(runtime.rpcUrl, /^ws:\/\/127\.0\.0\.1:\d+\/rpc$/);
    assert.notEqual(new URL(runtime.httpUrl).port, "8731", "the test must not use the device port");
    assert.notEqual(runtime.rpcAddr.split(":")[1], "8730", "the test must not use the device port");
  });

  it("serves the shell and its own discovery document", async () => {
    const health = await fetch(`${world.daemon.httpUrl}/healthz`);
    assert.equal(health.status, 200);

    const shell = await fetch(`${world.daemon.httpUrl}/`);
    assert.equal(shell.status, 200);
    assert.match(await shell.text(), /<html/i);

    // The shell must be able to find the socket without assuming a port, and
    // the policy that lets it connect has to name the port actually in use.
    assert.match(
      shell.headers.get("content-security-policy") ?? "",
      new RegExp(`connect-src ws://127\\.0\\.0\\.1:${new URL(world.daemon.rpcUrl).port}\\b`),
    );
    const document = await (await fetch(`${world.daemon.httpUrl}/system.json`)).json();
    assert.equal(document.rpcUrl, world.daemon.rpcUrl);
  });

  it("answers the mock HAL through system.info", async () => {
    const info = await world.shell.call("system.info");
    assert.equal(typeof info.version, "string");
    assert.ok(info.screen.width > 0 && info.screen.height > 0);
    assert.ok(["round", "square", "rect"].includes(info.screen.shape));
  });
});

describe("installing a signed package with integrity pins", () => {
  it("installs when the archive hash and publisher key both match", async () => {
    const result = await world.shell.call("apps.install", {
      source: world.signed.path,
      expectedSha256: world.signed.sha256,
      expectedPublisherKey: world.publisherKey,
      allowUnsigned: false,
    });
    assert.equal(result.appId, APP_ID);
    assert.equal(result.version, world.signed.version);
  });

  it("lists the app with its publisher recorded", async () => {
    const { apps } = await world.shell.call("apps.list");
    const record = apps.find((app) => app.id === APP_ID);
    assert.ok(record, "the installed app is missing from apps.list");
    assert.equal(record.signed, true);
    assert.equal(record.publisherKey, world.publisherKey);
    assert.equal(record.url, `/apps/${APP_ID}/index.html`);
  });

  it("serves the installed app's assets over the HTTP port", async () => {
    const entry = await fetch(`${world.daemon.httpUrl}/apps/${APP_ID}/index.html`);
    assert.equal(entry.status, 200);
    assert.match(await entry.text(), /<html/i);

    const bundle = await fetch(`${world.daemon.httpUrl}/apps/${APP_ID}/app.js`);
    assert.equal(bundle.status, 200);
    assert.match(bundle.headers.get("content-type") ?? "", /javascript/);
    // A sandboxed app may not open a socket of its own; the shell brokers.
    assert.match(bundle.headers.get("content-security-policy") ?? "", /connect-src 'none'/);

    // The daemon's own install record is not part of the app's asset surface.
    const meta = await fetch(`${world.daemon.httpUrl}/apps/${APP_ID}/install.json`);
    assert.equal(meta.status, 404);
  });
});

describe("wallet signing", () => {
  before(async () => {
    world.app = await RpcConnection.open(world.daemon.rpcUrl, APP_ID);
  });

  it("blocks until the wearer confirms, then returns a verifiable signature", async () => {
    const { publicKey } = await world.shell.call("wallet.publicKey");
    const message = Buffer.from("SolWear end-to-end transaction", "utf8");

    const pending = world.app.call("wallet.signTransaction", {
      appId: APP_ID,
      message: message.toString("base64"),
      label: "end-to-end test",
    });

    // The daemon must ask the shell before it does anything else.
    const prompt = await world.shell.waitForEvent("wallet.confirmRequest");
    assert.equal(prompt.params.appId, APP_ID);
    assert.equal(prompt.params.summary.byteLength, message.length);
    assert.equal(prompt.params.summary.digest, sha256(message));

    // And it must still be waiting: no confirmation, no signature.
    assert.equal(
      await settledWithin(pending, 750),
      "pending",
      "signTransaction returned before the wearer confirmed",
    );

    await world.shell.call("shell.confirmResponse", {
      requestId: prompt.params.requestId,
      approved: true,
    });
    const { signature } = await pending;

    const key = createPublicKey({
      key: Buffer.concat([SPKI_PREFIX, base58Decode(publicKey)]),
      format: "der",
      type: "spki",
    });
    assert.equal(
      verifySignature(null, message, key, base58Decode(signature)),
      true,
      "the signature does not verify against wallet.publicKey",
    );
  });

  it("returns an error rather than a signature when the wearer declines", async () => {
    const pending = world.app.send("wallet.signTransaction", {
      appId: APP_ID,
      message: Buffer.from("declined", "utf8").toString("base64"),
    });
    const prompt = await world.shell.waitForEvent("wallet.confirmRequest");
    await world.shell.call("shell.confirmResponse", {
      requestId: prompt.params.requestId,
      approved: false,
    });

    const response = await pending;
    assert.ok(response.error, "a declined request must not produce a signature");
    assert.equal(response.error.code, -32002);
    assert.equal(response.result, undefined);
  });
});

describe("the capability boundary", () => {
  it("refuses a method outside the app's manifest with -32001", async () => {
    // The e2e app declares system and wallet, so power is out of bounds.
    const error = await world.app.expectError("power.status");
    assert.equal(error.code, -32001);
    assert.equal(error.data.capability, "power");
    assert.equal(error.data.appId, APP_ID);
  });

  it("refuses the privileged shell namespace to an app", async () => {
    const error = await world.app.expectError("shell.ready");
    assert.equal(error.code, -32001);
  });

  it("does not let an app forge another app's identity on its own connection", async () => {
    const error = await world.app.expectError("system.info", {}, { appId: OTHER_APP_ID });
    assert.equal(error.code, -32600);
    assert.match(error.message, /does not match the identity/);
  });

  it("does not let an app sign on behalf of another app", async () => {
    const error = await world.app.expectError("wallet.signTransaction", {
      appId: OTHER_APP_ID,
      message: Buffer.from("not mine", "utf8").toString("base64"),
    });
    assert.equal(error.code, -32602);
    assert.match(error.message, /does not match the calling application/);
  });

  it("keeps a bound connection bound even when it claims to be the shell", async () => {
    const error = await world.app.expectError("shell.ready", {}, { appId: "tech.solwear.shell" });
    assert.equal(error.code, -32600);
  });
});

describe("packages that must be refused", () => {
  const install = (params) => world.shell.expectError("apps.install", params);

  it("rejects an archive tampered with after signing", async () => {
    const tampered = join(world.work, "tampered.swa");
    writeFileSync(tampered, repack(world.signed.path, "index.html", "<!doctype html><h1>replaced</h1>"));
    const error = await install({ source: tampered, allowUnsigned: false });
    assert.equal(error.code, -32005);
    assert.match(error.message, /modified|signature/i);
  });

  it("rejects a SHA-256 pin that does not match the archive", async () => {
    const error = await install({
      source: world.signed.path,
      expectedSha256: "0".repeat(63) + "1",
      expectedPublisherKey: world.publisherKey,
    });
    assert.equal(error.code, -32005);
    assert.match(error.message, /SHA-256 mismatch/);
  });

  it("rejects a package signed by a key the registry does not list", async () => {
    const error = await install({
      source: world.impostorSigned.path,
      expectedSha256: world.impostorSigned.sha256,
      expectedPublisherKey: world.publisherKey,
      allowUnsigned: false,
    });
    assert.equal(error.code, -32005);
    assert.match(error.message, /publisher key does not match/);
  });

  it("rejects an unsigned package on the store path", async () => {
    const error = await install({ source: world.unsigned.path, allowUnsigned: false });
    assert.equal(error.code, -32005);
    assert.match(error.message, /unsigned packages require developer sideload mode/);
  });

  it("accepts the same unsigned package on the sideload path", async () => {
    const result = await world.shell.call("apps.install", {
      source: world.unsigned.path,
      allowUnsigned: true,
    });
    assert.equal(result.appId, OTHER_APP_ID);

    const { apps } = await world.shell.call("apps.list");
    const record = apps.find((app) => app.id === OTHER_APP_ID);
    assert.equal(record.signed, false, "a sideloaded app must not be reported as signed");
    assert.equal(record.publisherKey, undefined);
  });

  it("leaves nothing installed for a package it refused", async () => {
    const { apps } = await world.shell.call("apps.list");
    assert.equal(
      apps.some((app) => app.id === "tech.solwear.e2e-impostor"),
      false,
    );
  });
});

describe("uninstalling", () => {
  it("removes the app and stops serving its assets", async () => {
    await world.shell.call("apps.uninstall", { appId: OTHER_APP_ID });
    const { apps } = await world.shell.call("apps.list");
    assert.equal(
      apps.some((app) => app.id === OTHER_APP_ID),
      false,
    );
    // Give the filesystem removal a moment before asking for an asset again.
    await delay(50);
    const response = await fetch(`${world.daemon.httpUrl}/apps/${OTHER_APP_ID}/index.html`);
    assert.equal(response.status, 404);
  });
});

describe("the registry the store app reads", () => {
  it("verifies every published package against its pinned hash and key", async () => {
    // The same command CI runs: a registry entry whose hash or signature does
    // not match the bytes it pins fails here too.
    const { stdout } = await run(process.execPath, [
      join(repoRoot, "store", "registry", "verify-packages.mjs"),
      "--offline",
    ]);
    assert.match(stdout, /Verified 3 packages/);
  });

  it("installs a first-party package straight from the registry entry", async () => {
    const index = JSON.parse(
      readFileSync(join(repoRoot, "store", "registry", "index.json"), "utf8"),
    );
    const entry = index.apps.find((app) => app.id === "tech.solwear.watchface");
    assert.ok(entry, "the registry must list the first-party watchface");

    // Exactly what the store app does, minus the download: pass the registry's
    // own pins to apps.install and let the daemon do the verifying.
    const result = await world.shell.call("apps.install", {
      source: join(repoRoot, "store", "registry", "packages", `${entry.id}-${entry.version}.swa`),
      expectedSha256: entry.sha256,
      expectedPublisherKey: entry.publisherKey,
      allowUnsigned: false,
    });
    assert.equal(result.appId, entry.id);
    assert.equal(result.version, entry.version);
  });
});
