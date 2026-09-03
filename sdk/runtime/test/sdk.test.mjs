import test from "node:test";
import assert from "node:assert/strict";
import { Bridge, TypedEmitter, layout, solwear, CAPABILITIES } from "../dist/index.js";

test("emitter delivers, unsubscribes and survives a throwing listener", () => {
  const emitter = new TypedEmitter();
  const seen = [];
  const off = emitter.on("tick", (value) => seen.push(value));
  emitter.on("tick", () => {
    throw new Error("listener failure must not stop delivery");
  });
  const later = [];
  emitter.on("tick", (value) => later.push(value));

  emitter.emit("tick", 1);
  off();
  emitter.emit("tick", 2);

  assert.deepEqual(seen, [1]);
  assert.deepEqual(later, [1, 2]);
});

test("once fires exactly once", () => {
  const emitter = new TypedEmitter();
  let count = 0;
  emitter.once("button", () => count++);
  emitter.emit("button", {});
  emitter.emit("button", {});
  assert.equal(count, 1);
});

test("layout derives units from the smaller dimension", () => {
  const wide = layout({ width: 800, height: 480, shape: "rect" });
  assert.equal(wide.base, 480);
  assert.equal(wide.u(0.5), 240);

  const small = layout({ width: 240, height: 240, shape: "square" });
  assert.equal(small.base, 240);
  assert.ok(small.rootFontSize >= 11, "root font size stays legible on tiny screens");
});

test("round screens get a larger safe inset than square ones", () => {
  const round = layout({ width: 480, height: 480, shape: "round" });
  const square = layout({ width: 480, height: 480, shape: "square" });
  assert.ok(round.safeInset > square.safeInset);
  assert.ok(round.safeInset < round.base / 4, "the inset must not eat the whole screen");
});

test("every capability in the spec is exported", () => {
  assert.deepEqual([...CAPABILITIES].sort(), [
    "apps",
    "display",
    "nfc",
    "notifications",
    "power",
    "sensors",
    "system",
    "wallet",
  ]);
});

test("outside a shell, an RPC call fails with an explanation instead of hanging", async () => {
  await assert.rejects(() => solwear.power.status(), /shell/i);
});

test("bridge interoperates with the device shell's solwear/kind wire format", async () => {
  const listeners = new Map();
  const sent = [];
  const parent = { postMessage: (message) => sent.push(message) };
  const previousWindow = globalThis.window;
  globalThis.window = {
    parent,
    innerWidth: 240,
    innerHeight: 240,
    addEventListener: (name, listener) => listeners.set(name, listener),
  };
  try {
    const bridge = new Bridge("test");
    listeners.get("message")({ source: parent, data: { solwear: 1, kind: "init", appId: "tech.solwear.test", capabilities: ["power"], screen: { width: 240, height: 240, shape: "round" } } });
    const context = await bridge.ready();
    assert.equal(context.attached, true);
    const power = bridge.call("power.status");
    await Promise.resolve();
    const request = sent.find((message) => message.kind === "rpc");
    assert.equal(request.solwear, 1);
    listeners.get("message")({ source: parent, data: { solwear: 1, kind: "result", id: request.id, result: { percent: 50 } } });
    assert.deepEqual(await power, { percent: 50 });
    let gesture;
    bridge.events.once("gesture", (event) => { gesture = event; });
    listeners.get("message")({ source: parent, data: { solwear: 1, kind: "event", event: "gesture", payload: { direction: "left" } } });
    assert.equal(gesture.gesture, "swipe-left");
  } finally {
    if (previousWindow === undefined) delete globalThis.window;
    else globalThis.window = previousWindow;
  }
});
