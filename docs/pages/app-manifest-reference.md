# App Manifest Reference

Every SolWear application ships a `manifest.json` at the root of its `.swa`
archive. It is the app's identity, its version, and its request for
capabilities. `solweard` reads it at install time; the store validates it when
you publish; the shell reads it to decide where the app belongs in the UI.

## Complete example

```json
{
  "id": "tech.solwear.watchface",
  "name": "Classic Watchface",
  "version": "1.0.0",
  "sdk": "0.1",
  "type": "watchface",
  "entry": "index.html",
  "icon": "assets/icon.png",
  "capabilities": ["system", "power"],
  "author": "SolWear",
  "description": "One sentence, shown in the store."
}
```

## Fields

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `id` | string | yes | Reverse-DNS application identifier. Immutable once published. |
| `name` | string | yes | Display name, shown in the launcher and the store. |
| `version` | string | yes | Semantic version, `MAJOR.MINOR.PATCH`. |
| `sdk` | string | yes | The `@solwear/sdk` line this app was built against. `"0.1"` today. |
| `type` | string | yes | `"app"` or `"watchface"`. |
| `entry` | string | yes | Path to the HTML entry point inside the archive. |
| `icon` | string | no | Path to the icon inside the archive. Strongly recommended for anything published. |
| `capabilities` | string[] | yes | The capability names this app is granted. May be empty. |
| `author` | string | yes | Publisher name, shown in the store. |
| `description` | string | yes | One sentence, shown in the store listing. |

### `id`

Reverse-DNS, lowercase, using a domain you control: `tech.solwear.watchface`,
`com.example.timer`. The registry enforces uniqueness across every published
app, and an id is immutable — it identifies the same application for the rest of
its life. Changing an id is publishing a different app, and the store will treat
it as one, with no upgrade path for existing users.

Accepted shape: two or more dot-separated segments, each starting with a letter
and containing only lowercase letters, digits and hyphens.

### `name`

The human-readable name. Keep it short: it is rendered in a launcher grid on a
screen that may be 240 pixels wide. Anything past roughly twenty characters will
be truncated.

### `version`

Strict semantic versioning, `MAJOR.MINOR.PATCH`, no `v` prefix and no build
metadata. The registry requires that each new entry for an id has a strictly
greater version than the highest already published for that id — you cannot
republish or downgrade a version, and the validator will reject an attempt.

The `.swa` file name is derived from the id and the version:
`tech.solwear.watchface-1.0.0.swa`.

### `sdk`

The SDK line the app was built against, as `MAJOR.MINOR`. The daemon uses it to
decide whether it can host the app at all, and to apply compatibility behaviour
for older apps. For v0.1 the only accepted value is `"0.1"`.

### `type`

| Value | Meaning |
| --- | --- |
| `app` | A normal application. Appears in the launcher, opened by the user, hosted full-screen. |
| `watchface` | A watchface. Selectable in settings, hosted in the watchface slot, shown when the device is idle. |

A watchface is expected to be cheap to render and to respect the `tick` throttle.
It is the thing on screen every time the user raises their wrist, so it is the
one app whose power behaviour is always visible.

### `entry`

A path inside the archive, relative to the archive root, to the HTML file that
is loaded into the sandboxed iframe. Almost always `index.html`. It must exist
in the package; `solwear package` fails if it does not.

### `icon`

A path inside the archive to a PNG. Square, and at least 192x192 so it stays
sharp on an 800x480 screen. Optional for sideloading, effectively required for
the store — an entry without an icon looks broken in the listing.

### `capabilities`

The list of capability names the app is granted. Each name maps to a JSON-RPC
method prefix:

| Capability | Methods it unlocks |
| --- | --- |
| `system` | `system.info`, `system.time`, `system.stats` |
| `power` | `power.status` |
| `display` | `display.setBrightness` |
| `sensors` | `sensors.read` |
| `notifications` | `notifications.list`, `notifications.post` |
| `apps` | `apps.list`, `apps.install`, `apps.uninstall`, `apps.launch` |
| `wallet` | public key, status, lock/unlock, activity and signing methods |
| `nfc` | `nfc.status`, `nfc.setEnabled`, `nfc.walletRecord`, `nfc.diagnostics` |

`solweard` rejects any call whose method prefix is outside this list with
JSON-RPC error code `-32001`. There is no runtime prompt to widen the set: what
the manifest declares at install time is what the app gets for its whole life.

Ask for the minimum. `apps` and `wallet` are the two that get read closely in
store review, because `apps` can install and uninstall software and `wallet`
touches the signing path. See
[Capabilities and Security](capabilities-and-security.md).

### `author`

The publisher's name as it should appear in the store. For store submissions it
must match the `publisher` field of the registry entry.

### `description`

One sentence. It is the line under your app's name in the store listing, on a
small screen, so it needs to say what the app does rather than why it is
exciting.

## Package layout

The manifest lives at the root of the `.swa` archive:

```
manifest.json      required
index.html         required, the app entry point
assets/            optional
signature.json     required for store distribution, optional for sideload
```

`signature.json` contains SHA-256 for every other archive file and an Ed25519
signature over their canonical, sorted listing. The whole `.swa`, including
`signature.json`, has a separate SHA-256 in the registry. See
[Package Format and Signing](package-format-and-signing.md) for the byte-exact
format, key encoding and verification order.

## Validation

Three things check your manifest, and they check different things:

1. **`solwear package`** — refuses to build an archive whose manifest is
   malformed, whose `entry` or `icon` does not exist, or whose `capabilities`
   contains an unknown name.
2. **`solweard`** — validates again at install time, then enforces the
   capability list for the life of the installation.
3. **The registry validator** — runs in CI on every publishing pull request,
   checking the manifest against
   [`store/registry/schema/manifest.schema.json`](https://github.com/SolWear/solwear-os/blob/main/store/registry/schema/manifest.schema.json)
   as well as id uniqueness and semver ordering. Registry CI additionally
   downloads published packages and verifies their whole-archive hash,
   manifest, publisher key and Ed25519 signature.

You can run the same check the store runs:

```bash
node store/registry/validate.mjs --manifest path/to/manifest.json
```

## Common mistakes

- **Calling a method you did not declare.** The failure is `-32001` at runtime,
  not at build time. If a call fails in the emulator, check `capabilities`
  before you check anything else.
- **Reusing a version.** The registry rejects a version that is not strictly
  greater than the highest already published for that id.
- **A `v` prefix on the version.** `1.0.0`, never `v1.0.0`.
- **An id you do not control.** Use a domain that is yours. Store review checks.
- **An `entry` pointing outside the archive.** Paths are relative to the archive
  root and may not escape it.
