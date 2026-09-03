# The `.swa` signature scheme

Three independent implementations have to agree on this byte for byte: the
`solwear` CLI that produces signatures, the `solweard` daemon that verifies at
install time, and the store app that verifies before it downloads. If any part
of this document changes, all three change together.

## The archive

A `.swa` file is a ZIP archive. The CLI writes it deterministically: entries
sorted by path, a fixed 2020-01-01 timestamp, deflate level 9 with a fallback to
stored when deflating would not make the file smaller. Packaging the same input
twice therefore produces byte-identical output and the same SHA-256, which is
what the registry entry records.

## What gets signed

1. Take every entry in the archive **except** `signature.json`.
2. Hash each one with SHA-256 and render it as lowercase hex.
3. Sort the paths with a plain byte-order comparison (`Array.prototype.sort` on
   the UTF-8 strings; the paths are ASCII in practice).
4. Build one line per file: the hex digest, two spaces, the path, a newline.
   This is the familiar `sha256sum` layout.
5. Prefix the whole block with the domain separator line
   `SolWear .swa signature v1\n`.

The result is the message. Sign it with Ed25519 (RFC 8032, no prehashing —
`crypto.sign(null, message, key)` in Node, `SigningKey::sign` in ed25519-dalek).

A two-file package hashes to exactly this:

```
SolWear .swa signature v1
5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03  index.html
ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  manifest.json
```

## signature.json

```json
{
  "version": 1,
  "algorithm": "ed25519",
  "publicKey": "<base64 of the raw 32-byte public key>",
  "signature": "<base64 of the raw 64-byte signature>",
  "files": { "index.html": "<hex>", "manifest.json": "<hex>" },
  "signedAt": "2026-01-01T00:00:00.000Z"
}
```

Keys and signatures travel as raw bytes in base64, not DER and not PEM, because
that is what ed25519-dalek on the device consumes directly.

## Verification

A verifier must do all of the following, and refuse the package if any of them
fails:

1. `signature.json` exists, parses, and has `version` 1 and `algorithm`
   `ed25519`.
2. Every file in the archive except `signature.json` appears in `files` with a
   matching digest. **A file present in the archive but absent from `files` is a
   failure**, otherwise anything could be smuggled in alongside a valid
   signature.
3. Every path in `files` is present in the archive. A missing file is a failure
   too, since a package that lost half its contents is not the package that was
   signed.
4. The signature verifies over the message rebuilt from `files`.
5. When the caller knows which key to expect — the store does, from
   `publisherKey` in the registry index — the key in `signature.json` matches
   it. Verifying against whatever key the package brought with it proves only
   that the package is internally consistent, not that it came from the
   publisher.

`solwear verify <file.swa>` runs exactly this and prints the reason on failure.
The unit tests in `test/package.test.mjs` cover each rejection path: a modified
file, an added file, a removed file, a swapped key, and no signature at all.

## Keys

`solwear keygen` writes:

```json
{
  "algorithm": "ed25519",
  "createdAt": "...",
  "publicKey": "<base64 32 bytes>",
  "privateKey": "<base64 32-byte seed>"
}
```

with mode 0600. `solwear sign --key` also accepts a PEM PKCS#8 Ed25519 key and a
Solana CLI keypair (a JSON array of 64 numbers, seed followed by public key), so
a publisher can reuse a key they already have.
