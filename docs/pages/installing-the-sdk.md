# Installing the SDK

The SolWear SDK is two pieces that you almost always install together:

- **`@solwear/sdk`** — the typed runtime your app imports. It gives you
  `solwear.system`, `solwear.power`, `solwear.display`, `solwear.sensors`,
  `solwear.notifications` and `solwear.wallet`, plus an event emitter.
- **`solwear`** — the command line tool that scaffolds, builds, runs, packages,
  signs, installs and publishes your app.

There is also an optional VS Code extension that wraps the same commands.

## Requirements

Node 22 LTS or newer. That is the only hard requirement for app development.
Rust is needed only if you are building the system daemon itself, and
`qemu-system-aarch64` only if you want to boot the full image rather than the
fast host emulator.

## Installing the command line tool

Globally, which is what most people want:

```bash
npm install --global @solwear/cli
solwear --version
```

Per project, which is better for CI and for pinning a version:

```bash
npm install --save-dev @solwear/cli
npx solwear --version
```

Verify the whole toolchain in one step:

```bash
solwear doctor
```

`doctor` checks Node, Rust, `qemu-system-aarch64`, an SSH client and your
signing key, and prints the exact install command for anything it cannot find.
Optional tools are reported as optional; a clean machine with only Node
installed should see zero required failures.

## Installing the runtime into an app

`solwear new` adds the dependency for you. To add it to an existing project:

```bash
npm install @solwear/sdk
```

Then import the client:

```ts
import { solwear } from "@solwear/sdk";

const info = await solwear.system.info();
const battery = await solwear.power.status();

console.log(info.device, info.screen.shape, battery.percent);
```

Every method is typed and returns a promise. Under the hood the SDK speaks the
JSON-RPC API through the bridge in the shell's sandbox — it never opens the
WebSocket itself, and it cannot, because the app runs in a sandboxed iframe with
no network access to the daemon. That indirection is what makes the capability
gate enforceable.

## The API surface at a glance

```ts
solwear.system.info();                       // version, device, screen
solwear.system.time();                       // epochMs, timezone
solwear.system.screen;                       // { width, height, shape }, synchronous

solwear.power.status();                      // percent, charging, estimateMinutes

solwear.display.setBrightness({ percent: 60 });

solwear.sensors.read({ sensor: "heartRate" });

solwear.notifications.list();
solwear.notifications.post({ title, body, appId });

solwear.wallet.publicKey();
solwear.wallet.signTransaction({ appId, message });
```

`solwear.system.screen` is a synchronous property rather than a call, because
layout code needs it on the first frame. Everything else is a promise.

## Events

```ts
solwear.on("tick", (t) => render(t));          // once per second
solwear.on("visibility", (v) => { /* v.visible */ });
solwear.on("button", (e) => { /* e.button, e.action */ });
solwear.on("gesture", (e) => { /* e.gesture, e.direction */ });
```

`tick` fires once per second and is the correct way to drive a clock. Do not run
your own `setInterval` against `Date.now()`: `tick` is throttled when the screen
is off, which is most of a watch's life and most of its battery.

`visibility` tells you when your app is backgrounded, and is where you stop
animations. `button` and `gesture` deliver hardware buttons and touch gestures
as the device reports them, normalised across device profiles.

## Declaring what you use

Every method belongs to a capability, and `solweard` rejects any call outside the
set granted in your manifest with JSON-RPC error `-32001`. If you call
`power.status()` you need `"power"` in `manifest.json`:

```json
{
  "id": "tech.example.hello-watch",
  "capabilities": ["system", "power"]
}
```

Ask for the smallest set that makes your app work. Reviewers of a store
submission will ask you to justify anything beyond that, and `wallet` in
particular gets read carefully. See
[Capabilities and Security](capabilities-and-security.md).

## The VS Code extension

```
Extensions -> search "SolWear" -> Install
```

It adds commands for `new`, `build`, `run`, `package` and `sign`, project
templates matching the CLI's, and a control for starting and stopping the host
emulator with a chosen device profile. It shells out to the same CLI, so nothing
it does is unavailable from a terminal.

## Working from the monorepo

If you are developing SolWear OS itself rather than an app on top of it, use the
workspace copies rather than the published packages:

```bash
git clone https://github.com/SolWear/solwear-os.git
cd solwear-os
for p in sdk/runtime sdk/cli os/shell; do npm --prefix "$p" install && npm --prefix "$p" run build; done
npm link ./sdk/cli
solwear --version          # resolves to sdk/cli in your working tree
```

## Next

[Your First Watchface](your-first-watchface.md) puts all of this to work.
