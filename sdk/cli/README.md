# solwear

The command line tool for SolWear OS. One tool covers the whole loop: create,
build, run, package, sign, install, publish.

## Install

Inside the monorepo:

```sh
cd sdk/cli && npm install && npm run build
npm link          # optional, puts `solwear` on your PATH
```

Everything below also works as `node sdk/cli/dist/bin.js <command>`.

## The loop

```sh
solwear doctor                          # check the toolchain first
solwear new my-face --template watchface
cd my-face
solwear run --profile pi-round-480      # host emulator, starts in about a second
solwear package                         # dist/<id>-<version>.swa
solwear keygen                          # once, ~/.solwear/publisher.key.json
solwear sign --key ~/.solwear/publisher.key.json
solwear verify dist/*.swa
solwear install --device solwear.local  # push to a real watch over SSH
solwear publish                         # prepare the registry entry
```

## Commands

| Command | What it does |
| --- | --- |
| `new <name> [--template watchface\|app\|signer]` | Scaffold a project that builds and packages with no edits. |
| `build [--watch] [--minify]` | Bundle `src/main.ts` with esbuild and stage `dist/`. |
| `run [--profile <name>]` | Build, then open the host emulator. `--list-profiles` shows the devices. |
| `run --qemu` | Boot the real aarch64 image under QEMU instead. |
| `package [--out <file>]` | Produce `dist/<id>-<version>.swa`. |
| `sign --key <path>` | Add `signature.json`. |
| `keygen [--out <path>]` | Generate an Ed25519 publisher key. |
| `verify <file.swa>` | Check a signature and list the contents. |
| `install --device <host>` | Build, package, verify, copy over SSH, ask the daemon to install. |
| `publish [--url <href>]` | Write a registry entry and explain the pull request. |
| `doctor [--json]` | Check node, rust, qemu, ssh, a browser and keys. |

Every command takes `--help`.

## How the build resolves the SDK

A project that has run `npm install` gets `@solwear/sdk` from `node_modules`. A
freshly generated project inside the monorepo has installed nothing, so the
build aliases the import straight at `sdk/runtime/dist`. That is what makes
`solwear new` produce something that builds immediately. Outside the monorepo
and without an install, the build stops and tells you to run `npm install
@solwear/sdk`.

## Packaging and signing

`.swa` is a ZIP archive written deterministically, so the same input always
hashes the same. `signature.json` holds an Ed25519 signature over the SHA-256 of
every other file. `SIGNING.md` documents the scheme exactly, because the daemon
and the store app implement the verifying half of it.

`package` writes into `dist/` as the specification requires, and skips any
`.swa` and any `.map` already there: a package must never contain a previous
package, and source maps roughly double what gets shipped to a watch.

## Errors

Anything a developer can fix is a `CliError` with a hint that names the exact
command to run. Anything else prints a stack and says it is a bug in the tool.
If you add a command, keep to that split.

## Tests

```sh
npm test
```

Covers the ZIP round trip, deterministic packaging, manifest validation,
argument parsing, and every signature rejection path (a modified file, an added
file, a removed file, a swapped key, no signature), plus a full
new → build → package → sign → verify run in a temporary directory.
