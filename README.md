# SolWear OS

**An operating environment for wearables, built on ordinary Linux, where every app is web content.**

SolWear started as a Solana hardware signer: a small screen you could trust to show
you what you were about to sign, with a private key that never left the device.
Building that product meant building a display stack, a system daemon, a sandbox
for the signing UI, and a way to ship updates. At that point the signer was one
app on a platform — so we made the platform the product.

SolWear OS is that platform. It runs on a Raspberry Pi 4 or 5 today, it renders
correctly on round and square screens from 240x240 to 800x480, and the original
signer now ships as one of five first-party apps on top of it.

> Status: v0.1, under active development. The architecture is settled and
> documented in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), which is the
> binding contract for every component in this repository.

---

## What you get

- **A real device target.** Raspberry Pi OS Lite (arm64, Bookworm), `cage` as a
  Wayland kiosk, Chromium in kiosk mode. No custom compositor, no Yocto, no
  kernel work.
- **One system daemon.** `solweard`, a single static Rust binary, owns the
  hardware abstraction layer, the app lifecycle, the keystore, and the API.
- **Apps in TypeScript, HTML and CSS.** The lowest barrier we could find for
  third-party developers, and content that adapts to any screen size for free.
- **A capability-gated sandbox.** Apps run in a sandboxed iframe and reach the
  system only through a bridge that enforces the capabilities in their manifest.
  A call outside the granted set is rejected with JSON-RPC error `-32001`.
- **Two useful emulation levels.** The host simulator boots the real shell and
  app bundles against a protocol-compatible mock daemon in under two seconds,
  with live HAL controls and RPC diagnostics. QEMU boots a real ARM64 Debian
  Linux kernel/userspace and runs the production Rust daemon under systemd.
- **A signed app store.** `.swa` packages, Ed25519 signatures over a canonical
  SHA-256 file list, and a registry where publishing is a reviewed pull request.

## Architecture

```
                        ┌──────────────────────────────────────────┐
   Developer machine    │  emulator/host — desktop window          │
   (macOS or Linux)     │  ┌────────────────────────────────────┐  │
                        │  │ shell UI + app bundle (real code)   │  │
                        │  └──────────────┬─────────────────────┘  │
                        │        JSON-RPC │ 2.0                    │
                        │  ┌──────────────┴─────────────────────┐  │
                        │  │ solweard  (SOLWEAR_HAL=mock)        │  │
                        │  └────────────────────────────────────┘  │
                        └──────────────────────────────────────────┘
                                          ▲
                    identical UI code, only the HAL is swapped
                                          ▼
   ┌──────────────────────────────────────────────────────────────────────┐
   │  Raspberry Pi 4 / 5 — Raspberry Pi OS Lite arm64                     │
   │                                                                      │
   │  ┌────────────────────────────────────────────────────────────────┐  │
   │  │ cage (Wayland kiosk) → Chromium kiosk → http://127.0.0.1:8731  │  │
   │  │                                                                │  │
   │  │  ┌──────────────┐  ┌──────────────────────────────────────┐    │  │
   │  │  │ os/shell     │  │ sandboxed iframe: one app            │    │  │
   │  │  │ watchface    │  │  apps/watchface · apps/signer ·      │    │  │
   │  │  │ host,        │◀─▶  apps/store, using @solwear/sdk      │    │  │
   │  │  │ launcher,    │  │                                      │    │  │
   │  │  │ tray,        │  └──────────────┬───────────────────────┘    │  │
   │  │  │ confirm UI   │   capability-gated bridge                    │  │
   │  │  └──────┬───────┘                 │                            │  │
   │  └─────────┼─────────────────────────┼────────────────────────────┘  │
   │            │  JSON-RPC 2.0 over WebSocket  ws://127.0.0.1:8730       │
   │  ┌─────────▼─────────────────────────▼────────────────────────────┐  │
   │  │ solweard (Rust, systemd)                                       │  │
   │  │   ├─ JSON-RPC server        :8730   ├─ app lifecycle + install │  │
   │  │   ├─ static asset HTTP      :8731   ├─ signature verification  │  │
   │  │   └─ HAL: PiHal | MockHal           └─ Solana keystore         │  │
   │  └───────────┬────────────────────────────────────────────────────┘  │
   │              │ sysfs battery · I2C sensors · backlight · nmcli       │
   └──────────────┴───────────────────────────────────────────────────────┘
                                          ▲
                                          │  fetch index, verify SHA-256 + signature
                            store/registry/index.json  (pull-request publishing)
```

The consequence worth internalising: **the shell and every app are ordinary web
content.** The emulator runs the identical UI code the device runs, and only the
hardware abstraction layer is swapped underneath it.

## Quick start

You need Node 22 or newer and a stable Rust toolchain. Nothing else, and no
hardware.

Each component is an independent npm package, so build the three you need and
put the CLI on your path:

```bash
git clone https://github.com/SolWear/solwear-os.git && cd solwear-os
cargo build --manifest-path os/solweard/Cargo.toml
for p in sdk/runtime sdk/cli os/shell; do npm --prefix "$p" install && npm --prefix "$p" run build; done
npm link ./sdk/cli                           # puts `solwear` on your PATH
solwear doctor                               # verify node, rust, qemu, ssh, keys
solwear new my-watchface --template watchface
cd my-watchface && solwear run               # host emulator, real shell, mock HAL
```

That is a running watchface on a simulated round 480x480 screen. To package and
sign it:

```bash
solwear package                          # dist/<id>-<version>.swa
solwear sign --key ~/.solwear/publisher.key.json
solwear install --device solwear.local   # push to a real watch over SSH
```

The documentation site covers each of these in depth:

```bash
node docs/build.mjs && open docs/dist/index.html
```

## Repository map

| Path | What lives there |
| --- | --- |
| `os/solweard/` | Rust system daemon: JSON-RPC server, HAL, app lifecycle, keystore |
| `os/shell/` | TypeScript system shell: watchface host, launcher, notifications, settings |
| `sdk/cli/` | The `solwear` command line tool (Node/TypeScript) |
| `sdk/runtime/` | `@solwear/sdk` — the typed app API shipped to developers |
| `sdk/vscode/` | VS Code extension: commands, templates, emulator control |
| `emulator/host/` | Fast PC simulator — shell plus mock HAL in a desktop window |
| `emulator/qemu/` | Full aarch64 image boot under `qemu-system-aarch64` |
| `apps/watchface/` | Six switchable faces ported from the ESP32 product |
| `apps/signer/` | Solana signer and recent signing activity |
| `apps/store/` | System app: browses the registry, installs `.swa` packages |
| `apps/stats/` | Steps, heart rate, temperature and Linux resource statistics |
| `apps/games/` | Ping Pong, Tetris and Tamagotchi |
| `store/registry/` | The signed app registry, its JSON Schemas, and the validator |
| `image/` | Raspberry Pi image build scripts and systemd units |
| `tests/e2e/` | The end-to-end test: the whole stack in one run, no hardware and no QEMU |
| `docs/` | The architecture specification and the documentation site source |

## Documentation

| Page | |
| --- | --- |
| [Architecture Specification](docs/ARCHITECTURE.md) | The binding contract for every component |
| [Getting Started](docs/pages/getting-started.md) | From clone to a running emulator |
| [Installing the SDK](docs/pages/installing-the-sdk.md) | Toolchain, `@solwear/sdk`, the CLI |
| [Your First Watchface](docs/pages/your-first-watchface.md) | A complete app, start to finish |
| [App Manifest Reference](docs/pages/app-manifest-reference.md) | Every field of `manifest.json` |
| [Package Format and Signing](docs/pages/package-format-and-signing.md) | `.swa` ZIP layout and byte-exact Ed25519 scheme |
| [JSON-RPC API Reference](docs/pages/json-rpc-api-reference.md) | Every method, with request and response examples |
| [Capabilities and Security](docs/pages/capabilities-and-security.md) | The sandbox, the capability model, the keystore |
| [Using the Emulator](docs/pages/using-the-emulator.md) | Host simulator and QEMU, device profiles |
| [Publishing to the Store](docs/pages/publishing-to-the-store.md) | Packaging, signing, the registry pull request |
| [Flashing a Raspberry Pi](docs/pages/flashing-a-raspberry-pi.md) | Building the image and booting real hardware |
| [ESP32 Migration Status](docs/LEGACY_MIGRATION.md) | Ported features and the hardware validation boundary |

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). It covers branch and commit
conventions, how to run every component locally, and what CI will check before a
human reviews your pull request.

Publishing an app to the store is also a pull request — see
[`store/registry/README.md`](store/registry/README.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).
