# Contributing to SolWear OS

Thank you for working on SolWear. This document covers how the repository is
organised for contributors, how to run everything locally, and the conventions
your pull request will be reviewed against.

Before you change behaviour anywhere, read
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). It is the single source of truth
for this monorepo. The OS, the SDK, the emulator and the store are built
independently against that contract, so a change that contradicts it breaks
components you may not be looking at. If you believe the specification is wrong,
change the specification first, in its own pull request, and say why.

## Ground rules

- **The specification wins.** The JSON-RPC surface in section 4.2, the manifest
  in section 5, and the registry format in section 10 are contracts. Adding a
  method or a field means amending the specification in the same change.
- **No component may require hardware to build or to test.** Everything must run
  on macOS and on arm64 Linux with only Node and Rust installed.
- **Every HAL method must work under `MockHal`.** A method that only exists on
  real hardware is a bug, not a feature.
- **Nothing assumes a fixed screen size.** The display is adaptive; sizes come
  from `system.screen`, never from constants.
- **Signing keys never enter the repository.** `.gitignore` blocks the obvious
  cases, but the responsibility is yours.

## Prerequisites

| Tool | Version | Notes |
| --- | --- | --- |
| Node.js | 20 LTS or newer | npm workspaces are used across `os/shell`, `sdk/`, `apps/` |
| Rust | stable, current | `rustfmt` and `clippy` components required |
| Git | 2.30 or newer | |
| `qemu-system-aarch64` | optional | Only for `solwear run --qemu` and the QEMU emulator tests |

Install the Rust components once:

```bash
rustup component add rustfmt clippy
rustup target add aarch64-unknown-linux-gnu   # only if you cross-build the daemon
```

## Running everything locally

Components are independent npm packages rather than one workspace, so install
and build the ones you are working on. From a clean clone:

```bash
for p in sdk/runtime sdk/cli os/shell emulator/host apps/*; do
  [ -f "$p/package.json" ] && npm --prefix "$p" install
done
```

### The daemon

```bash
cargo build  --manifest-path os/solweard/Cargo.toml
cargo test   --manifest-path os/solweard/Cargo.toml
cargo fmt    --manifest-path os/solweard/Cargo.toml --all -- --check
cargo clippy --manifest-path os/solweard/Cargo.toml --all-targets -- -D warnings
```

Run it against the mock HAL, which is what the emulator does:

```bash
SOLWEAR_HAL=mock cargo run --manifest-path os/solweard/Cargo.toml
# JSON-RPC on ws://127.0.0.1:8730, static assets on http://127.0.0.1:8731
```

### The shell, the SDK, the CLI and the apps

```bash
npm --prefix sdk/runtime run build
npm --prefix sdk/cli     run build && npm --prefix sdk/cli test
npm --prefix os/shell    run build && npm --prefix os/shell run typecheck
```

CI walks every directory that has a `package.json` and runs `lint`, `typecheck`,
`build` and `test` in each, skipping any script a package does not define. Doing
the same locally over the components you touched is the quickest way to predict
what CI will say.

### The emulator

```bash
npm --prefix emulator/host start                       # default device profile
npm --prefix emulator/host start -- --profile pi-round-480
```

Ship-blocking profiles are `pi-round-480`, `pi-square-320` and `pi-wide-800x480`.
A layout change is not finished until it has been looked at on all three.

### The registry

```bash
node store/registry/validate.mjs                  # validate the live index
node store/registry/test.mjs                      # schema and cryptographic rejection cases
node store/registry/verify-packages.mjs --offline # verify the packages checked in under packages/
node store/registry/verify-packages.mjs           # the same, downloading anything not held locally
```

### The end-to-end test

```bash
tests/e2e/run.sh              # build what it needs, then run it
tests/e2e/run.sh --no-build   # everything is already built
```

One run starts the compiled daemon against the mock HAL on ephemeral ports,
installs a `.swa` the real CLI built and signed, drives a wallet signature
through a confirmation, and checks every refusal the daemon owes: a tampered
archive, a wrong hash pin, a wrong publisher key, an unsigned package on the
store path, a capability violation, and one app claiming to be another. It needs
Node 22 or newer for the global WebSocket client, and no hardware or QEMU.

### The documentation site

```bash
node docs/build.mjs      # renders docs/pages into docs/dist
node docs/serve.mjs      # http://127.0.0.1:4321
```

The docs build has no dependencies beyond Node itself and must stay that way.

## Branches

Work on a branch off `main`. Name it `<type>/<short-slug>`, using the same type
vocabulary as commit messages:

```
feat/wallet-confirm-timeout
fix/round-screen-safe-inset
docs/json-rpc-error-codes
chore/bump-actions
```

`main` is protected. Everything lands through a pull request, and the branch is
deleted after merge.

## Commits

Conventional Commits, with a scope naming the component:

```
<type>(<scope>): <imperative summary under 72 characters>

<body: what changed and why, wrapped at 72 columns>

<footer: Refs #123 / Closes #123 / BREAKING CHANGE: ...>
```

Types: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `build`, `ci`, `chore`.

Scopes: `solweard`, `shell`, `sdk`, `cli`, `vscode`, `emulator`, `apps`,
`registry`, `image`, `docs`, `ci`.

Examples:

```
feat(solweard): reject wallet calls without the wallet capability
fix(shell): derive the round-screen inset from the smaller dimension
docs(sdk): document the gesture event payload
```

Keep commits focused. A commit that touches the daemon, the shell and the docs
because they genuinely move together is fine; a commit that does three unrelated
things is not. Rebase to clean up before review rather than pushing fixup
commits on top.

A commit that changes the JSON-RPC surface, the manifest format or the registry
format must include the corresponding specification and documentation changes.

## Pull requests and review

A pull request should say what changed, why, and how you verified it. If it
touches UI, include screenshots from at least the round and the square profile.

Before you request review:

- [ ] `build`, `test` and `lint` pass in every component you touched.
- [ ] `cargo test`, `cargo fmt --check` and `cargo clippy -D warnings` pass.
- [ ] `node store/registry/test.mjs` passes if you touched the registry.
- [ ] `node store/registry/verify-packages.mjs` passes if you changed registry
      entries (it downloads anything not checked in under `packages/`).
- [ ] `tests/e2e/run.sh` passes if you touched the daemon, the CLI, the package
      format or the capability model.
- [ ] `node docs/build.mjs` passes if you touched the docs.
- [ ] New behaviour has tests. Section 12 of the specification lists the
      coverage the project is held to: JSON-RPC method contracts, capability
      enforcement, `.swa` packaging round-trip, signature verification including
      rejection of a tampered package, and adaptive layout at each device
      profile.
- [ ] The specification is updated if a contract changed.

CI runs the same checks on every push and pull request; a red build will not be
reviewed. One approving review from a maintainer is required to merge. Reviewers
look for contract conformance first, then correctness, then clarity. Squash
merge is the default; use a merge commit only when the individual commits are
genuinely worth keeping.

Security-relevant changes — anything touching the keystore, signature
verification, the capability gate or the sandbox — need a second reviewer.

## Reporting a security issue

Do not open a public issue for a vulnerability in the keystore, the signing path,
the capability gate or the package verifier. Email `security@solwear.tech` with
the details and give us a chance to ship a fix first.

## License

By contributing you agree that your contributions are licensed under the
Apache License 2.0, as set out in [LICENSE](LICENSE).
