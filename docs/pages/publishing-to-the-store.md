# Publishing to the Store

The SolWear store is a signed registry in this repository. There is no upload
form and no dashboard: publishing an app is a pull request that adds an entry to
`store/registry/index.json`, and CI validates it before a human reviews it.

That is a deliberate choice. The registry is the thing devices trust to decide
what to install, so every change to it should be reviewable, attributable and
revertable, which is exactly what a pull request gives you.

## How installation works, and why the registry looks like this

The store app on the device fetches `index.json`, and for each app it shows the
name, version, publisher and description from that file. The trusted store
installation path:

1. downloads the package from `url`;
2. computes its SHA-256 and compares it to `sha256`;
3. requires `signature.json.publicKey` to equal `publisherKey`, recomputes every
   signed file digest and verifies the Ed25519 signature;
4. validates the manifest;
5. only then installs.

A failure at any step aborts the install. There is no override. Every field in a
registry entry exists to support one of those steps.

## Before you submit

Your app needs to be built, packaged and signed, and hosted somewhere stable.

```bash
solwear build
solwear package                                   # dist/<id>-<version>.swa
solwear sign --key ~/.solwear/publisher.key.json  # adds signature.json
```

If you do not have a signing key yet:

```bash
solwear keygen --out ~/.solwear/publisher.key.json
```

Keep the private key out of the repository and out of CI logs. The public half
is what goes in the registry entry as `publisherKey`; the private half signs
every version you will ever publish under that key, so losing it means losing
the ability to ship updates to your users.

Take the hash of the exact file you are going to host:

```bash
shasum -a 256 dist/tech.example.timer-1.2.0.swa    # macOS
sha256sum dist/tech.example.timer-1.2.0.swa        # Linux
```

Host the file at a stable HTTPS URL. It must stay byte-identical forever: the
hash in the registry pins it, and a re-upload that changes a single byte breaks
installation for everyone.

## The registry entry

Add one object to the `apps` array in `store/registry/index.json`:

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
| `id` | Reverse-DNS, must match the package manifest exactly, immutable across versions |
| `name` | Must match the manifest |
| `version` | Strict `MAJOR.MINOR.PATCH`, must match the manifest, must be strictly greater than every version already published for this id |
| `type` | `app` or `watchface`, must match the manifest |
| `url` | HTTPS, stable, byte-identical forever, file name `<id>-<version>.swa` |
| `sha256` | 64 lowercase hex characters, the digest of the file at `url` |
| `publisher` | Must match the manifest's `author` |
| `publisherKey` | Canonical Base64 of the raw 32-byte Ed25519 key in `signature.json`. Must not change between versions of the same id |
| `description` | One sentence, must match the manifest |
| `sdk` | The SDK line, `"0.1"` today |

Entries for the same id are kept together and in ascending version order. The
validator enforces that ordering, so a new version is appended after the
previous one for that id rather than dropped at the end of the file.

## Validate before you push

Run exactly what CI runs:

```bash
node store/registry/validate.mjs
node store/registry/test.mjs
node store/registry/verify-packages.mjs
```

The first two commands are offline: they check JSON Schema, id/version
uniqueness and ordering, hash/key shape, manifest rules and rejection fixtures.
The final command uses the network. It downloads every hosted `.swa`, verifies
the complete archive SHA-256, validates and cross-checks its manifest, requires
complete per-file digest coverage, compares the embedded key with
`publisherKey`, and verifies Ed25519. Any command exits non-zero on failure.

```
store/registry/index.json
  /apps/3/sha256   must be 64 lowercase hexadecimal characters
  /apps/3/version  1.1.0 is not greater than 1.2.0, already published for tech.example.timer
2 problems found
```

You can also check the manifest on its own:

```bash
node store/registry/validate.mjs --manifest dist/manifest.json
```

## Open the pull request

```bash
git checkout -b publish/tech.example.timer-1.2.0
git add store/registry/index.json
git commit -m "feat(registry): publish tech.example.timer 1.2.0"
git push origin publish/tech.example.timer-1.2.0
```

The pull request should contain **only** the registry change. A pull request
that also touches the daemon, the shell or the docs will be asked to split.

In the description, include what the app does, the capabilities it requests and
why it needs each one, and the output of your local `shasum -a 256` so a
reviewer can compare it against the hosted file.

## What review looks at

CI runs the offline validation and downloads every package to verify the hosted
bytes, manifest, publisher key and Ed25519 signature. A red build is not
reviewed.

A human then looks at:

- **Capabilities.** Every one you request has to be justified by what the app
  does. `wallet`, `apps`, `sensors` and `notifications` are read closely, because
  they touch signing, software installation, health data and the whole
  notification tray respectively.
- **Identity.** The `id` uses a domain you control, and `publisher` matches the
  manifest's `author`.
- **Key continuity.** For an update, `publisherKey` is the same key that signed
  every previous version of this id. A changed key is a red flag and needs an
  explanation before it will be merged.
- **The listing.** The name and the one-sentence description describe the app
  honestly and fit on a small screen.

## Publishing an update

Same process, new entry. Do not edit an existing entry: bump the version, host a
new file, and append. Devices that installed the old version keep working, and
the history of what was ever published stays in the file and in git.

An existing entry may only be edited to fix a factual error in metadata that
does not change what gets installed — a typo in the description, for instance.
Changing `url` or `sha256` on a published entry is never acceptable.

## Unpublishing

Removing an entry stops new installs. It does not uninstall the app from devices
that already have it, and it does not make the hosted file disappear. If you
need something pulled for a security reason, say so in the pull request title
and email `security@solwear.tech` so a maintainer sees it quickly.

## Reference

The full publishing flow, the schemas and the validator's fixture cases live in
[`store/registry/README.md`](https://github.com/SolWear/solwear-os/blob/main/store/registry/README.md).
The byte-exact package contract is in
[Package Format and Signing](package-format-and-signing.md).
