# Capabilities and Security

SolWear runs third-party code on a device that holds a private key. The security
model is therefore not an afterthought bolted onto an app platform; it is the
reason the platform is shaped the way it is.

This page describes the sandbox, the capability gate, the keystore rules and the
package signing chain, and it is honest about what each of them does not
protect against.

## The layers

```
┌──────────────────────────────────────────────────────────────┐
│ app: sandboxed iframe, no network to the daemon, no host DOM │
│      speaks only postMessage to the bridge                   │
├──────────────────────────────────────────────────────────────┤
│ bridge in the shell: knows the calling app's id, checks the  │
│      method prefix against the installed manifest            │
├──────────────────────────────────────────────────────────────┤
│ solweard: re-checks the capability, owns the keystore, forces│
│      user confirmation for signing                           │
├──────────────────────────────────────────────────────────────┤
│ package verification: SHA-256 + Ed25519 before anything is   │
│      written to disk                                         │
└──────────────────────────────────────────────────────────────┘
```

No single layer is trusted to be sufficient. The bridge check is what makes
denials fast and legible; the daemon check is what makes them true.

## The sandbox

Every app runs in a sandboxed iframe served by `solweard` from
`http://127.0.0.1:8731`. Inside that frame an app cannot:

- reach the host document, the shell's DOM, or any other app's frame;
- open a WebSocket to `127.0.0.1:8730` and call the API directly;
- read or write another app's installed files;
- persist anything outside its own storage partition.

What it can do is render, use browser storage scoped to its own origin, and call
`@solwear/sdk`. The SDK does not hold a socket. It posts messages to the bridge
in the shell, and the bridge makes the call on the app's behalf. That indirection
is the whole design: because the app cannot reach the socket, the capability
check on the way through cannot be skipped.

## The capability model

An app declares its capabilities in `manifest.json`:

```json
{ "capabilities": ["system", "power"] }
```

Capability names map to JSON-RPC method prefixes:

| Capability | Methods | What it really grants |
| --- | --- | --- |
| `system` | `system.info`, `system.time` | Device model, OS version, screen geometry, clock, timezone |
| `power` | `power.status` | Battery percentage, charge state, runtime estimate |
| `display` | `display.setBrightness` | Control of the backlight |
| `sensors` | `sensors.read` | Sensor readings, including heart rate |
| `notifications` | `notifications.list`, `notifications.post` | Read of the whole tray, and the ability to post |
| `apps` | `apps.list`, `apps.install`, `apps.uninstall`, `apps.launch` | Enumerating, installing, removing and launching software |
| `wallet` | `wallet.publicKey`, `wallet.signTransaction` | The public key, and the ability to request a signature |

A call whose prefix is not in the granted set is rejected with JSON-RPC error
`-32001`:

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "error": {
    "code": -32001,
    "message": "capability denied",
    "data": { "method": "wallet.signTransaction", "required": "wallet", "appId": "tech.example.timer" }
  }
}
```

Three properties are worth being explicit about:

1. **Capabilities are fixed at install time.** There is no runtime prompt that
   widens the set. An app gets exactly what its manifest declared when the user
   installed it, for the whole life of that installation.
2. **Changing them requires a new version.** The user sees the new capability
   list before the upgrade, which is the point.
3. **The gate is enforced twice.** The bridge checks, and `solweard` checks
   again. An app that somehow bypassed the bridge would still be refused.

### Capabilities worth arguing about

`sensors` includes heart rate, which is health data. `notifications` grants
read access to the entire tray, not just to notifications your app posted —
that is a deliberate simplification in v0.1 and a real disclosure, so treat it as
sensitive. `apps` can install and uninstall software. `wallet` touches the
signing path. Store review reads all four closely and will ask you to justify
them.

Ask for the smallest set that makes your app work. A watchface that shows time
and battery needs `["system", "power"]` and nothing else.

## The keystore and the signing path

The Solana private key lives in `solweard` and never leaves it. It is not
exposed over the API, not readable by any app, and not accessible to the shell.
The only two things that cross the boundary are the public key and a signature
over a message you supplied.

`wallet.signTransaction` has a rule with no exceptions:

> The call MUST always raise a confirmation prompt on the device screen, and MUST
> never sign without an affirmative user action.

There is no development flag, no settings toggle and no headless mode that
bypasses it. The prompt is rendered by the shell, not by the requesting app, and
it shows which app is asking and what the decoded transaction does. An app
cannot style it, cover it, dismiss it, or pre-answer it.

If the user declines or the prompt times out, the call returns an error rather
than a signature. Handle that path: declining is a normal outcome.

This is the property the original SolWear hardware signer existed to provide.
Everything else in the platform is arranged so that adding apps does not weaken
it.

## Package integrity

A `.swa` archive for store distribution contains `signature.json`: SHA-256 for
every other file and an Ed25519 signature over the canonical sorted digest
listing. The registry separately pins SHA-256 of the complete archive and the
expected signing public key. See
[Package Format and Signing](package-format-and-signing.md).

The trusted store path, before installation:

1. downloads the package;
2. computes its SHA-256 and compares it to the registry entry;
3. requires the embedded Base64 public key to equal registry `publisherKey`,
   recomputes every per-file digest and verifies Ed25519;
4. validates the manifest, including that every declared capability is a known
   name;
5. only then writes to disk.

The daemon independently validates the manifest and any embedded signature
before writing the package. Any failure aborts the install. There is no
"install anyway" path in the store app. Tampering with a single byte — including
inside `assets/` — changes a digest and fails verification; registry tests cover
wrong archive hashes, modified and added files, and publisher-key mismatches.

Sideloading via `solwear install --device` accepts an unsigned package, because
you cannot develop otherwise. Such an app is recorded with `"signed": false` and
`apps.list` reports it that way, so the distinction stays visible.

## What this does not protect against

Being clear about the boundaries is more useful than implying there are none.

- **A compromised `solweard` compromises everything.** It holds the key. The
  sandbox protects the daemon from apps, not the system from the daemon.
- **Physical access.** v0.1 does not encrypt the root filesystem. Someone
  holding the device with tools can reach storage.
- **A malicious publisher key.** Signing proves a package came from a given key
  and was not modified in transit. It does not prove the code behind that key is
  benign. That is what store review is for.
- **Loopback callers.** A process already running on the device can open
  `ws://127.0.0.1:8730` and call the API without a capability gate, because the
  gate applies to app calls arriving through the bridge. The threat model
  assumes an uncompromised host; if arbitrary local processes are running, the
  device is already lost.
- **Side channels.** Apps share a CPU and a display. Timing and resource-use
  inference are not addressed in v0.1.

## Reporting a vulnerability

Do not open a public issue for a problem in the keystore, the signing path, the
capability gate or the package verifier. Email `security@solwear.tech` and give
us a chance to ship a fix first.
