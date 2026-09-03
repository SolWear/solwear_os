# @solwear/sdk

The typed application API for SolWear OS. Every SolWear app is ordinary web
content running inside a sandboxed iframe hosted by the shell; this package is
how that content talks to the system.

## Install

Inside the monorepo the CLI resolves the SDK for you, so a generated project
needs no install step. Outside it:

```sh
npm install @solwear/sdk
```

## Use

```ts
import { solwear, layout } from "@solwear/sdk";

await solwear.ready();                        // handshake with the shell
const metrics = layout(solwear.system.screen); // publishes CSS variables

const battery = await solwear.power.status();
solwear.on("tick", (t) => render(t));          // one event per second
```

If you cannot use a bundler, load `dist/solwear-sdk.global.js` with a plain
script tag; it defines `window.solwear`.

## Namespaces

| Namespace | Methods |
| --- | --- |
| `system` | `info()`, `time()`, `stats()`, `screen` (synchronous, valid after `ready()`) |
| `power` | `status()` |
| `display` | `setBrightness(percent)` |
| `sensors` | `read(sensor)` |
| `notifications` | `list()`, `post({ title, body })` |
| `apps` | `list()`, `install(source)`, `uninstall(id)`, `launch(id)` |
| `wallet` | `publicKey()`, `status()`, `lock()`, `unlock()`, `activity()`, `signTransaction()` |
| `nfc` | `status()`, `setEnabled()`, `walletRecord()`, `diagnostics()` |

## Events

`tick` (once a second), `visibility`, `button`, `gesture`. Subscribe with
`solwear.on(name, listener)`; the returned function unsubscribes.

## Capabilities

A namespace is only reachable if the app's `manifest.json` lists it in
`capabilities`. The SDK checks locally and fails fast with a readable message,
and `solweard` enforces the same rule again with JSON-RPC error `-32001`. The
two layers are deliberate: the local check gives developers a good error, the
daemon check is the one that provides the security.

## Transport

The SDK never opens the JSON-RPC WebSocket. It posts messages to the parent
window using the `solwear.bridge/1` protocol defined in `src/protocol.ts`, and
the shell forwards approved calls to `solweard` on `ws://127.0.0.1:8730`. An app
therefore has no handle on the socket and cannot reach a method the shell has
not agreed to forward.

Opened outside a shell (a plain browser tab, for instance) the SDK reports a
detached context: layout still works and `tick` still fires from a local timer,
but every RPC call rejects with an explanation instead of hanging.

## Layout

The display is adaptive from 240x240 up to 800x480, round or square. `layout()`
returns the metrics and sets `--sw-w`, `--sw-h`, `--sw-base` and `--sw-safe` on
the root element, plus a `data-shape` attribute. Size everything from those:
never from a pixel constant.

## Scripts

```sh
npm run build   # tsc to dist/ with declarations, then the browser bundles
npm test        # node:test unit tests against dist/
```
