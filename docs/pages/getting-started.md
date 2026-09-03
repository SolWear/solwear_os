# Getting Started

This page takes you from a clean machine to a SolWear application running in the
host emulator. No hardware is required, and none of the steps here need a
Raspberry Pi, a display, or a Solana wallet.

## Prerequisites

| Tool | Version | Why |
| --- | --- | --- |
| Node.js | 20 LTS or newer | The shell, the SDK, the CLI, the apps and the emulator |
| Rust | current stable | The `solweard` system daemon |
| Git | 2.30 or newer | |
| `qemu-system-aarch64` | optional | Only needed for the full image emulator |

Everything in SolWear OS builds and tests on macOS and on arm64 Linux. If a
component ever needs hardware to build or to run its tests, that is a bug worth
reporting.

Check what you have:

```bash
node --version     # v20.x or newer
cargo --version    # 1.75 or newer
```

## Clone and build

```bash
git clone https://github.com/SolWear/solwear-os.git
cd solwear-os
cargo build --manifest-path os/solweard/Cargo.toml
for p in sdk/runtime sdk/cli os/shell; do npm --prefix "$p" install && npm --prefix "$p" run build; done
npm link ./sdk/cli
```

Each component of the monorepo is an independent npm package. `cargo build`
produces the daemon; the loop builds the SDK runtime, the command line tool and
the shell; `npm link` puts `solwear` on your path.

If you only want to write apps rather than work on the OS itself, skip all of
that and `npm install --global @solwear/cli`. See
[Installing the SDK](installing-the-sdk.md).

## Check your toolchain

```bash
solwear doctor
```

`doctor` verifies Node, Rust, `qemu-system-aarch64`, an SSH client and your
signing keys, and prints the exact command to install anything missing. It is
designed to pass on a clean machine that has only Node and Rust, reporting the
optional pieces as optional rather than as failures.

Typical output:

```
solwear doctor
  node                 v20.11.1          ok
  rust                 1.83.0            ok
  qemu-system-aarch64  not found         optional — brew install qemu
  ssh                  OpenSSH_9.6p1     ok
  signing key          ~/.solwear/publisher.key.json   not found — run: solwear keygen
2 optional items missing, 0 required
```

## Create an app

```bash
solwear new hello-watch --template watchface
cd hello-watch
```

Templates are `watchface`, `app` and `signer`. You get a directory laid out like
this:

```
hello-watch/
  manifest.json      app identity, version, capabilities
  src/main.ts        your code
  src/style.css
  index.html         entry point
  assets/icon.png
```

## Run it

```bash
solwear run
```

This starts the host emulator: a desktop window that renders the real system
shell with your app loaded inside it, backed by the mock hardware abstraction
layer. It starts in under two seconds and reloads when you edit a file.

Pick a different screen while you are working — an adaptive layout is not
finished until you have looked at it on a round screen and a square one:

```bash
solwear run --profile pi-round-480
solwear run --profile pi-square-320
solwear run --profile pi-wide-800x480
```

The emulator window draws the device bezel, so a round screen is visibly round
and you can see immediately when content lands in the clipped corners.

## Run the daemon by itself

Sometimes you want the API without the UI, for example to poke at it with a
WebSocket client:

```bash
SOLWEAR_HAL=mock cargo run --manifest-path os/solweard/Cargo.toml
```

The daemon then serves JSON-RPC on `ws://127.0.0.1:8730` and static assets on
`http://127.0.0.1:8731`. `SOLWEAR_HAL=mock` selects the deterministic fake HAL,
whose battery level, clock and sensor readings can be scripted from a JSON file.
See [Using the Emulator](using-the-emulator.md) for the script format.

## Package it

```bash
solwear build
solwear package                 # dist/tech.example.hello-watch-0.1.0.swa
```

A `.swa` file is a ZIP archive containing `manifest.json`, `index.html`, an
optional `assets/` directory, and — once you sign it — `signature.json`. To sign
and push to a real device:

```bash
solwear sign --key ~/.solwear/publisher.key.json
solwear install --device solwear.local
```

Sideloading accepts an unsigned package. Store distribution does not.

## Where to go next

- [Your First Watchface](your-first-watchface.md) writes a real adaptive
  watchface line by line.
- [JSON-RPC API Reference](json-rpc-api-reference.md) lists everything the
  daemon can do.
- [Publishing to the Store](publishing-to-the-store.md) covers signing and the
  registry pull request.
