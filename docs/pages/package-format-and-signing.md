# Package Format and Signing

A SolWear application is distributed as a `.swa` file. The extension is
SolWear-specific; the bytes are an ordinary ZIP archive. This page defines the
v1 format byte for byte so the CLI, store and daemon can implement it
independently.

## Archive layout

```text
manifest.json      required, exactly one at the archive root
index.html         required, exactly one at the archive root
assets/            optional application files
signature.json     required for store distribution; optional for sideloading
```

Every non-directory entry except `signature.json` is covered by the signature,
regardless of which directory it is in. Paths use `/`, are relative, contain no
empty, `.` or `..` segment, and use printable ASCII for portable store packages.
Duplicate paths, encrypted entries, multi-disk ZIP and ZIP64 are rejected. The
installed uncompressed contents may not exceed 64 MiB or 4096 entries.

The manifest's `entry` must exist. `index.html` remains required in v1 even if
`entry` names another HTML file.

`solwear package` writes deterministic ZIPs: paths are sorted, timestamps are
fixed, and compression settings are stable. Rebuilding identical input
therefore produces the same archive SHA-256.

## Two different hashes

Do not confuse these values:

- The registry's `sha256` is SHA-256 of the complete `.swa` byte stream,
  including `signature.json`. It pins the exact hosted download.
- `signature.json.files` contains one SHA-256 per uncompressed archive file,
  excluding `signature.json`. Those digests form the Ed25519 message and ensure
  that adding, removing or changing any application file invalidates the
  signature.

All SHA-256 values are lowercase hexadecimal.

## The signed message

To build the exact v1 message:

1. Hash every regular archive entry except `signature.json` with SHA-256.
2. Sort paths in ascending byte order. Store package paths are ASCII, so this is
   also ordinary string order.
3. Write `<digest>`, two ASCII spaces, `<path>`, and a newline for each file.
4. Prefix the block with `SolWear .swa signature v1\n`.

For a package with two files, the bytes to sign look like this:

```text
SolWear .swa signature v1
5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03  index.html
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  manifest.json
```

Sign that UTF-8 byte sequence with Ed25519 as defined by RFC 8032, without an
additional prehash.

## `signature.json`

```json
{
  "version": 1,
  "algorithm": "ed25519",
  "publicKey": "<base64 raw 32-byte Ed25519 public key>",
  "signature": "<base64 raw 64-byte Ed25519 signature>",
  "files": {
    "index.html": "<lowercase SHA-256 hex>",
    "manifest.json": "<lowercase SHA-256 hex>"
  },
  "signedAt": "2026-01-01T00:00:00.000Z"
}
```

`publicKey` is canonical Base64 of exactly 32 raw bytes. It is not PEM, DER or
Base58. The registry copies the exact same string into `publisherKey`, allowing
a verifier to compare identities before it verifies the signature. `signature`
is canonical Base64 of exactly 64 raw bytes.

`signedAt` is informational and is not part of the signed message. The file
digests, paths and signature are authoritative.

## Verification order

A store verifier performs all of these checks before installation:

1. Hash the downloaded `.swa` bytes and compare them with registry `sha256`.
2. Parse the ZIP with the path, duplicate, entry-count and size limits above.
3. Require `manifest.json`, `index.html` and `signature.json`.
4. Validate the manifest and compare its identity and listing fields with the
   registry entry.
5. Require `signature.json.files` to cover every other file exactly and compare
   every per-file digest.
6. Require `signature.json.publicKey` to equal registry `publisherKey`.
7. Rebuild the canonical message and verify the Ed25519 signature.

The package is rejected if any check fails. Verifying with only the public key
carried inside the package proves internal consistency, not publisher identity;
the registry key comparison is therefore mandatory for store installs.

Run the same checks locally:

```bash
solwear verify dist/tech.example.timer-1.2.0.swa
node store/registry/validate.mjs
node store/registry/verify-packages.mjs
```

The final command downloads every URL in the selected registry and is expected
to use the network. The schema validator remains offline and fast.

## Keys and sideloading

Generate a publisher key once and keep its private seed outside the repository:

```bash
solwear keygen --out ~/.solwear/publisher.key.json
solwear sign --key ~/.solwear/publisher.key.json
```

`solwear keygen` stores a Base64 32-byte private seed and its Base64 public key.
The signer also accepts PKCS#8 Ed25519 PEM and Solana CLI keypair JSON, but the
package and registry always use the raw public key encoded as Base64.

Unsigned packages are allowed only for explicit developer sideloading. Store
distribution always requires `signature.json` and the registry hash/key checks.
