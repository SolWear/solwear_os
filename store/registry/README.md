# SolWear App Registry

This directory is the SolWear app store. There is no upload form and no
dashboard: `index.json` is the list of published applications, and publishing an
app means opening a pull request that adds an entry to it.

That is deliberate. The registry is the file devices trust when deciding what to
install, so every change to it should be reviewable, attributable and
revertable. A pull request gives all three for free.

## Contents

| Path | What it is |
| --- | --- |
| `index.json` | The published registry. Section 10 of the architecture specification. |
| `schema/registry.schema.json` | JSON Schema for `index.json`. |
| `schema/manifest.schema.json` | JSON Schema for an app's `manifest.json`. Section 5 of the specification. |
| `validate.mjs` | The validator. This is what CI runs. |
| `verify-packages.mjs` | Verifies every listed `.swa` — its hash, manifest and Ed25519 signature — from `packages/` or from `url`. |
| `packages/` | The first-party `.swa` files the index pins, so a clean clone can verify them offline. |
| `test.mjs` | Fixture tests proving the validator rejects what it should. |
| `lib/jsonschema.mjs` | A small dependency-free JSON Schema validator. |
| `fixtures/valid/` | A registry and a manifest that must pass. |
| `fixtures/invalid/` | One file per rejection case, each tampered in exactly one way. |

Everything here runs on a clean clone with nothing installed but Node 20. There
are no dependencies, and there should not be any: the validator gates what gets
installed on people's devices, and its supply chain should stay empty.

`index.json` lists the three first-party applications. Their signed packages
are checked in under `packages/`, named `<id>-<version>.swa`, which is what
lets a clean clone verify the pinned hashes and signatures with no network at
all. Nothing is hosted at `packages.solwear.tech` yet, so `url` names where each
file will live rather than where it already lives; the entries are otherwise
real, and every hash and signature in them verifies against the bytes in
`packages/`. Placeholder URLs, hashes or publisher keys must never be published
merely to seed the catalogue.

Third-party packages are not checked in. A publisher hosts their own file and
`verify-packages.mjs` downloads it.

## How a device uses this file

The store app fetches `index.json` and lists what it finds. The trusted store
installation path:

1. downloads the package from `url`;
2. computes its SHA-256 and compares it to `sha256`;
3. requires `signature.json.publicKey` to equal `publisherKey`, recomputes every
   signed file digest and verifies the Ed25519 signature;
4. validates the manifest;
5. only then writes anything to disk.

A failure at any step aborts the install, and there is no override. Every field
in an entry exists to support one of those steps, which is why the validator is
strict about all of them.

## An entry

```json
{
  "id": "tech.example.timer",
  "name": "Interval Timer",
  "version": "1.2.0",
  "type": "app",
  "url": "https://apps.example.com/tech.example.timer-1.2.0.swa",
  "sha256": "9f2c1e0b7a5d4c3b2a190807f6e5d4c3b2a190807f6e5d4c3b2a190807f6e5d4",
  "publisher": "Example Ltd",
  "publisherKey": "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=",
  "description": "Interval timer with haptic cues.",
  "sdk": "0.1"
}
```

| Field | Rule |
| --- | --- |
| `id` | Reverse-DNS, matches the manifest exactly, immutable across versions |
| `name` | Matches the manifest, and stays the same across versions of an id |
| `version` | Strict `MAJOR.MINOR.PATCH`, matches the manifest, strictly greater than every version already published for this id |
| `type` | `app` or `watchface`, matches the manifest, may not change across versions |
| `url` | HTTPS, ends in `/<id>-<version>.swa`, byte-identical forever |
| `sha256` | 64 lowercase hex characters, the digest of the file at `url` |
| `publisher` | Matches the manifest's `author`, stable across versions |
| `publisherKey` | Canonical Base64 of the raw 32-byte Ed25519 key in `signature.json`, stable across versions |
| `description` | One sentence, matches the manifest |
| `sdk` | The SDK line the app targets, `"0.1"` today |

Entries for one id are kept **contiguous and ascending**. A new version is
appended directly after the previous version of the same app, not dropped at the
end of the file. The validator enforces this, because a reviewer should be able
to see an app's whole history in one place in the diff.

## The publishing flow

### 1. Build, package and sign

```bash
solwear build
solwear package                                   # dist/<id>-<version>.swa
solwear sign --key ~/.solwear/publisher.key.json  # adds signature.json
```

If you do not have a key yet, run
`solwear keygen --out ~/.solwear/publisher.key.json`.
Keep the private half out of the repository and out of CI logs. It signs every
version you will ever ship under that identity; losing it means losing the
ability to update your users.

### 2. Host the package

Put the `.swa` at a stable HTTPS URL named `<id>-<version>.swa`. It has to stay
byte-identical forever, because `sha256` pins it. Take the digest of the exact
file you are hosting:

```bash
shasum -a 256 dist/tech.example.timer-1.2.0.swa    # macOS
sha256sum dist/tech.example.timer-1.2.0.swa        # Linux
```

### 3. Add the entry

Edit `index.json`, adding your entry after the previous version of the same id,
or at the end if this is a new app.

### 4. Validate locally

Run exactly what CI runs:

```bash
node store/registry/validate.mjs
node store/registry/validate.mjs --manifest dist/manifest.json
node store/registry/test.mjs
node store/registry/verify-packages.mjs
```

The first three forms never touch the network. `verify-packages.mjs` resolves
each entry from `store/registry/packages/<id>-<version>.swa` when that file
exists and downloads `url` when it does not, then verifies the exact bytes it
loaded. It prints which of the two it used for every entry, so a local pass is
never mistaken for proof that the hosted file is correct. Two flags pin the
behaviour down:

```bash
node store/registry/verify-packages.mjs --offline            # local files only, never download
node store/registry/verify-packages.mjs --no-packages-dir    # always download, ignore local copies
```

Use `--offline` in CI and while preparing a change; use `--no-packages-dir`
once the package is hosted, to confirm that what is served matches what was
reviewed.

The validator exits non-zero on any problem and prints a JSON pointer to the
offending value:

```
store/registry/index.json
  /apps/3/sha256    must be 64 lowercase hexadecimal characters
  /apps/3/version   1.1.0 is not greater than 1.2.0, already published for tech.example.timer at /apps/2; versions for one id must ascend

2 problems found
```

### 5. Open the pull request

```bash
git checkout -b publish/tech.example.timer-1.2.0
git add store/registry/index.json
git commit -m "feat(registry): publish tech.example.timer 1.2.0"
git push origin publish/tech.example.timer-1.2.0
```

The pull request must contain **only** the registry change. Include in the
description:

- what the app does;
- every capability it requests and why it needs each one;
- the output of your local `shasum -a 256`, so a reviewer can compare it against
  the hosted file.

### 6. Review

CI runs schema/semantic validation, then downloads every package and checks its
whole-archive SHA-256, manifest, complete per-file digest set, publisher key and
Ed25519 signature. A red build is not reviewed.

A maintainer then checks:

- **Capabilities.** Each one has to be justified by what the app does. `wallet`,
  `apps`, `sensors` and `notifications` are read closely — they touch signing,
  software installation, health data and the entire notification tray.
- **Identity.** The `id` uses a domain you control and `publisher` matches the
  manifest's `author`.
- **Key continuity.** For an update, `publisherKey` is the same key that signed
  every earlier version. The validator flags a change; a human decides whether
  the explanation is good enough.
- **The listing.** Name and description are honest and fit a small screen.

## Updating an app

Add a new entry. Never edit a published one. Bump the version, host a new file,
append after the previous version.

The only acceptable edit to a published entry is a factual correction to
metadata that does not change what gets installed — fixing a typo in a
description, for example. Changing `url` or `sha256` on a published entry is
never acceptable: devices have already trusted those values.

## Unpublishing

Removing an entry stops new installs. It does not uninstall the app from devices
that already have it, and it does not remove the hosted file. If something needs
pulling for a security reason, say so in the pull request title and email
`security@solwear.tech` so a maintainer sees it immediately.

## What the validator checks

Schema conformance for both `index.json` and any manifest you pass, plus the
semantic rules a schema cannot express:

- every `id` and `version` pair is unique;
- entries for one id are contiguous and strictly ascending in semver order;
- every entry carries a well-formed, non-placeholder SHA-256 digest;
- every entry carries canonical Base64 for a raw 32-byte Ed25519
  `publisherKey`;
- `publisher`, `publisherKey`, `type` and `name` stay constant across versions of
  an id;
- `url` is HTTPS and names the id and version it claims to carry;
- a manifest passed with `--manifest` agrees with the registry entry that claims
  to describe it;
- manifest paths do not escape the archive, and a watchface does not request the
  `wallet` capability.

`validate.mjs` does **not** read packages at all. `verify-packages.mjs` is the
half that does: it verifies the archive hash,
portable ZIP structure, required files, manifest agreement, complete per-file
hash set, publisher identity and Ed25519 signature. Keeping the two commands
separate leaves the schema validator fast and useful offline.

## Running the tests

```bash
node store/registry/test.mjs
```

The suite has twenty-one schema/semantic cases plus cryptographic `.swa` cases.
The real registry and good fixtures must pass, and every invalid fixture must be
rejected for the intended reason. It also builds a signed ZIP in memory and
proves that the verifier rejects a wrong archive hash, a changed or added file,
and a registry publisher-key mismatch.

Each invalid fixture is the valid one with exactly one thing tampered with, so
the diff between `fixtures/valid/index.json` and any invalid file shows precisely
what is being tested.

When you add a rule to the validator, add a fixture that proves it fires.

## Reference

- [Publishing to the Store](../../docs/pages/publishing-to-the-store.md)
- [App Manifest Reference](../../docs/pages/app-manifest-reference.md)
- [Package Format and Signing](../../docs/pages/package-format-and-signing.md)
- [Capabilities and Security](../../docs/pages/capabilities-and-security.md)
- [Architecture Specification](../../docs/ARCHITECTURE.md), sections 5 and 10
