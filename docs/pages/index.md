# SolWear OS Documentation

SolWear OS is an operating environment for wearables. It runs on a Raspberry Pi
4 or 5, it renders correctly on round and square screens from 240x240 up to
800x480, and every application on it — including the system shell — is ordinary
web content running in a sandbox.

SolWear began as a Solana hardware signer. Building a device you could trust to
show you what you were about to sign meant building a display stack, a system
daemon, an app sandbox and an update path. Once that existed, the signer was
just one app on a platform, so the platform became the product. The signer still
ships, as one of three first-party apps.

## How the pieces fit together

| Component | Language | Role |
| --- | --- | --- |
| `solweard` | Rust | System daemon. JSON-RPC server on `127.0.0.1:8730`, static assets on `127.0.0.1:8731`, hardware abstraction layer, app lifecycle, keystore. |
| Shell | TypeScript | System UI. Watchface host, launcher, notification tray, settings, wallet confirmation prompt. |
| `@solwear/sdk` | TypeScript | The typed client apps import. Speaks JSON-RPC through the shell's bridge, never directly to the socket. |
| `solwear` CLI | Node | One tool for the whole loop: scaffold, build, run, package, sign, install, publish. |
| Host emulator | TypeScript | A desktop window running the real shell and the real app bundle against a mock HAL. |
| Registry | JSON | The signed app index. Publishing is a reviewed pull request. |

The decision that makes all of this work is that the shell and every app are web
content. The emulator runs the same UI code the device runs; only the hardware
abstraction layer underneath is swapped. There is no separate simulator build of
your application to keep in sync.

## Where to go next

- **[Getting Started](getting-started.md)** — from a clean machine to a running
  emulator in a few minutes.
- **[Installing the SDK](installing-the-sdk.md)** — the toolchain, `@solwear/sdk`
  and the `solwear` command line tool.
- **[Your First Watchface](your-first-watchface.md)** — a complete adaptive
  watchface, written and packaged from scratch.
- **[App Manifest Reference](app-manifest-reference.md)** — every field of
  `manifest.json` and how it is validated.
- **[Package Format and Signing](package-format-and-signing.md)** — the `.swa`
  ZIP layout, canonical signing message and verification order.
- **[JSON-RPC API Reference](json-rpc-api-reference.md)** — every method the
  daemon exposes, with request and response examples.
- **[Capabilities and Security](capabilities-and-security.md)** — the sandbox,
  the capability gate, and the rules around the keystore.
- **[Using the Emulator](using-the-emulator.md)** — device profiles, mock sensor
  scripts, and the QEMU path.
- **[Publishing to the Store](publishing-to-the-store.md)** — packaging, signing
  and the registry pull request.
- **[Flashing a Raspberry Pi](flashing-a-raspberry-pi.md)** — building the image
  and booting real hardware.

The [Architecture Specification](../ARCHITECTURE.md) is the binding contract for
every component in the repository. When this documentation and the specification
disagree, the specification is right and the documentation is a bug.

## Scope of v0.1

Target hardware is a Raspberry Pi 4 or 5 development board on arm64, running
Raspberry Pi OS Lite (Bookworm), with `cage` as a Wayland kiosk and Chromium in
kiosk mode.

Explicitly out of scope for this release: a custom compositor, a custom kernel, a
Yocto image, a real Bluetooth pairing stack, and on-chain mainnet transactions.
