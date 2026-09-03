# SolWear OS — Architecture Specification (v0.1)

This document is the single source of truth for the SolWear OS monorepo. Every
component described here must match this contract exactly, because the OS, the
SDK, the emulator, and the store are built independently against it.

## 1. Product goal

SolWear is a wearable ecosystem. SolWear OS is the on-device operating
environment, comparable in role to watchOS or Wear OS, but built on ordinary
Linux so that it can ship on Raspberry Pi hardware today.

Target hardware for this iteration: Raspberry Pi 4 / Raspberry Pi 5 development
board, arm64. The display is **adaptive**: every UI surface must render
correctly on round and square screens from 240x240 up to 800x480. Nothing may
assume a fixed pixel size.

Non-goals for v0.1: custom compositor, custom kernel, Yocto image, real
Bluetooth pairing stack, on-chain mainnet transactions.

## 2. Platform decisions (already made, do not revisit)

| Decision | Choice | Reason |
| --- | --- | --- |
| Base OS | Raspberry Pi OS Lite (arm64, Bookworm) | Standard Linux, apt, working drivers |
| Display stack | `cage` (Wayland kiosk) + Chromium in kiosk mode | No custom compositor to write or maintain |
| System services | One Rust daemon, `solweard` | Memory safe, single static binary, no runtime deps |
| App language | TypeScript / HTML / CSS | Lowest barrier for third-party developers, adapts to any screen for free |
| App isolation | Sandboxed iframe + capability-gated bridge | Apps cannot touch the host or each other |
| IPC | JSON-RPC 2.0 over a localhost WebSocket | Same protocol on device and in the emulator |

The key consequence of these choices: **the shell and every app are ordinary web
content**. That means the PC emulator runs the identical UI code the device
runs, and only the hardware abstraction layer is swapped.

## 3. Repository layout

```
os/solweard/      Rust system daemon (JSON-RPC server, HAL, app lifecycle)
os/shell/         TypeScript system shell UI (watchface host, launcher, notifications)
sdk/cli/          `solwear` command line tool (Node/TypeScript)
sdk/runtime/      @solwear/sdk — typed app API, shipped to app developers
sdk/vscode/       VS Code extension (commands, templates, emulator control)
emulator/host/    Fast PC simulator: shell + mock HAL in a desktop window
emulator/qemu/    Full aarch64 image boot under QEMU
apps/watchface/   Demo app: default watchface
apps/signer/      Demo app: Solana transaction signer (the original SolWear product)
apps/store/       System app: browses the registry, installs .swa packages
apps/stats/       Health readings and Linux runtime statistics
apps/games/       Ping Pong, Tetris and Tamagotchi
store/registry/   Signed app registry (JSON index + package hosting rules)
image/            Raspberry Pi image build scripts and systemd units
docs/             Developer documentation site
```

## 4. System daemon: `solweard`

A single Rust binary. Started by systemd before the UI. Responsibilities:

1. Serve the JSON-RPC 2.0 API over a WebSocket on `127.0.0.1:8730`.
2. Serve shell and app static assets over HTTP on `127.0.0.1:8731`.
3. Own the hardware abstraction layer (HAL).
4. Manage installed app packages: install, verify signature, list, uninstall.
5. Hold the keystore for the Solana signer, gated behind explicit user confirm.

### 4.1 HAL

The HAL is a Rust trait with two implementations selected at startup:

- `PiHal` — real hardware: battery via sysfs, sensors via I2C, backlight via
  `/sys/class/backlight`, network via `nmcli`.
- `MockHal` — deterministic fake used by the emulator and by tests. Enabled with
  `SOLWEAR_HAL=mock`. Values are scriptable from a JSON file so tests can drive
  battery level, time, and sensor readings.

Every HAL call must work under `MockHal`; a method that only exists on hardware
is a bug.

### 4.2 JSON-RPC API surface

Namespaced methods. All parameters and results are JSON objects, never
positional arrays.

```
system.info            -> { version, device, screen: { width, height, shape } }
system.time            -> { epochMs, timezone }
system.stats           -> { uptimeMs, platform, memory, storage, load, apps, notifications, shellConnected }
power.status           -> { percent, charging, estimateMinutes }
display.setBrightness  { percent }            -> {}
sensors.read           { sensor }             -> { sensor, value, unit, timestampMs }
notifications.list     -> { items: [...] }
notifications.post     { title, body, appId } -> { id }
apps.list              -> { apps: [ AppRecord ] }
apps.install           { source, expectedSha256?, expectedPublisherKey? }
                                                -> { appId, version }
apps.uninstall         { appId }              -> {}
apps.launch            { appId }              -> {}
wallet.publicKey       -> { publicKey }
wallet.status          -> { onboarded, locked, protected, name, publicKey }
wallet.setPassphrase   { passphrase, name? }  -> {}
wallet.lock            -> {}
wallet.unlock          { passphrase }         -> {}
wallet.activity        -> { items: [ WalletActivity ] }
wallet.signTransaction { appId, message }     -> { signature }   // requires user confirm on device
```

`wallet.signTransaction` MUST always raise a confirmation prompt on the device
screen and MUST never sign without an affirmative user action. The private key
never leaves `solweard` and is never exposed over the API.

### 4.3 Capabilities

Each app declares capabilities in its manifest. `solweard` rejects any RPC call
whose method is outside the calling app's granted capabilities, with JSON-RPC
error code `-32001`. Capability names map to method prefixes:

`system`, `power`, `display`, `sensors`, `notifications`, `apps`, `wallet`, `nfc`.

## 5. App package format (`.swa`)

A `.swa` file is a ZIP archive:

```
manifest.json      required
index.html         required, app entry point
assets/            optional
signature.json     required for store distribution, optional for sideload
```

`manifest.json`:

```json
{
  "id": "tech.solwear.watchface",
  "name": "Classic Watchface",
  "version": "1.0.0",
  "sdk": "0.1",
  "type": "app",
  "entry": "index.html",
  "icon": "assets/icon.png",
  "capabilities": ["system", "power"],
  "author": "SolWear",
  "description": "One sentence, shown in the store."
}
```

`type` is `app` or `watchface`. Ids are reverse-DNS and immutable.
`signature.json` uses signature format version 1:

```json
{
  "version": 1,
  "algorithm": "ed25519",
  "publicKey": "<canonical base64 of the raw 32-byte Ed25519 public key>",
  "signature": "<canonical base64 of the raw 64-byte signature>",
  "files": {
    "index.html": "<lowercase SHA-256 hex>",
    "manifest.json": "<lowercase SHA-256 hex>"
  },
  "signedAt": "2026-01-01T00:00:00.000Z"
}
```

Every regular file except `signature.json` MUST appear exactly once in `files`.
The signed bytes are the UTF-8 domain separator
`SolWear .swa signature v1\n`, followed by one line per file in ascending path
order: `<lowercase-sha256>  <path>\n` (two spaces). Ed25519 signs that message
directly, without another prehash. Adding, removing or changing any archive file
therefore invalidates the signature. `signedAt` is informational and is not
signed.

The registry's `sha256` has a different scope: it is SHA-256 of the complete
`.swa` byte stream, including `signature.json`. Registry `publisherKey` is the
exact Base64 string from `signature.json.publicKey`, not a Base58 Solana address.
The complete portable-path and verification rules are in
[Package Format and Signing](pages/package-format-and-signing.md).

## 6. App runtime API (`@solwear/sdk`)

Apps import a typed client. Under the hood it speaks the JSON-RPC API through
the bridge in the shell's sandbox, never directly to the socket.

```ts
import { solwear } from "@solwear/sdk";

const battery = await solwear.power.status();
solwear.on("tick", (t) => render(t));          // one event per second
const screen = solwear.system.screen;           // { width, height, shape }
```

The SDK must expose: `system`, `power`, `display`, `sensors`, `notifications`,
`apps`, `wallet`, `nfc`, plus an event emitter with the events `tick`, `visibility`,
`button`, and `gesture`. Every method is typed and returns a promise.

## 7. Shell

The shell is the system UI: watchface host, app launcher, notification tray,
settings, and the wallet confirmation prompt. It is a TypeScript single-page
app served by `solweard`.

Layout rules, mandatory because the display is adaptive:

- The root sizes itself from `system.screen`, never from constants.
- Round screens get a safe inset so no content lands in the clipped corners.
- All typography and spacing use units derived from the smaller screen
  dimension.
- Target 60fps on a Pi 4; avoid layout thrash and heavy shadows.

## 8. SDK CLI (`solwear`)

Modelled on `esp-idf`'s developer experience: one tool covers the whole loop.

```
solwear new <name> [--template watchface|app|signer]
solwear build                 # bundle TypeScript, emit dist/
solwear run                   # launch the host emulator with this app loaded
solwear run --qemu            # boot the full aarch64 image under QEMU
solwear package               # produce dist/<id>-<version>.swa
solwear sign --key <path>     # add signature.json
solwear install --device <host>   # push .swa to a real watch over SSH
solwear publish               # submit to the registry
solwear doctor                # verify toolchain: node, qemu, ssh, keys
```

## 9. Emulator

Two levels, both required.

**Host simulator** (`emulator/host`): a desktop window that renders the real
shell plus the real app bundle, backed by `MockHal`. Starts in under two
seconds. Device profiles are JSON files describing screen size, shape, buttons,
and mock sensor scripts; ship at least `pi-round-480`, `pi-square-320`, and
`pi-wide-800x480`. The window frame must draw the device bezel so round screens
are visibly round.

**QEMU emulator** (`emulator/qemu`): boots a real Debian Bookworm ARM64 guest
with `qemu-system-aarch64`, production `solweard` under systemd, and port
forwards to the daemon. It uses a UEFI/virtio disk rather than the Pi firmware
image; physical Pi peripherals remain a hardware test. It must detect a missing
`qemu-system-aarch64` and print the exact install command rather than failing
with a stack trace.

## 10. Store and registry

`store/registry/index.json` lists published apps:

```json
{
  "schemaVersion": 1,
  "apps": [
    {
      "id": "tech.solwear.watchface",
      "name": "Classic Watchface",
      "version": "1.0.0",
      "type": "watchface",
      "url": "https://…/tech.solwear.watchface-1.0.0.swa",
      "sha256": "…",
      "publisher": "SolWear",
      "publisherKey": "…"
    }
  ]
}
```

The store app on the device fetches the index and passes the registry's
`sha256` and `publisherKey` pins to `apps.install`. Both the store and daemon
verify the complete archive SHA-256, require its embedded public key to equal
`publisherKey`, and verify the v1 Ed25519 signature before install. They refuse
anything that fails. Publishing is a pull request that adds an entry; CI
downloads the hosted package and independently validates the manifest, archive
hash, complete per-file hash set, publisher key and signature.

## 11. Image build

`image/` holds a script that takes a Raspberry Pi OS Lite arm64 image and adds:
the `solweard` binary, shell assets, preinstalled system apps, a `solwear`
system user, systemd units (`solweard.service`, `solwear-ui.service` running
cage plus Chromium in kiosk mode against `http://127.0.0.1:8731`), read-only
root where practical, and first-boot Wi-Fi provisioning.

## 12. Quality bar

- Everything runs on macOS for development and on arm64 Linux for the device.
- `solwear doctor` passes on a clean machine with only Node and Rust installed.
- No component may require hardware to build or to run its tests.
- Tests cover: JSON-RPC method contracts, capability enforcement, `.swa`
  packaging round-trip, signature verification (including rejection of a
  tampered package), and adaptive layout at each shipped device profile.
