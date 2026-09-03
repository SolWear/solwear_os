# JSON-RPC API Reference

`solweard` exposes a JSON-RPC 2.0 API over a WebSocket on `127.0.0.1:8730`. It
is the complete system interface: the shell uses it, the emulator uses it, and
every app reaches it through `@solwear/sdk` and the shell's capability-gated
bridge.

This page documents every method in the surface, with a request and a response
example for each.

## Conventions

- **Transport.** JSON-RPC 2.0 over a WebSocket at `ws://127.0.0.1:8730`. The
  same protocol is spoken on the device and in the emulator; nothing about a
  client needs to change between them.
- **Parameters are always objects.** Every method takes and returns a JSON
  object, never a positional array. A method with no parameters takes `{}` or
  omits `params`; a method with no result returns `{}`.
- **Namespacing.** The part of the method name before the dot is its capability.
  `power.status` requires the `power` capability.
- **Apps do not connect directly.** An app calls `@solwear/sdk`, which posts to
  the bridge in the shell's sandbox, which makes the call. The socket is not
  reachable from inside an app's iframe, which is what makes the capability gate
  enforceable rather than advisory.

### Finding the socket

The two ports above are what a device uses, and what everything defaults to.
They can be moved, which a test harness or a second instance on a developer
machine needs:

| Variable | Default | What it sets |
| --- | --- | --- |
| `SOLWEAR_RPC_ADDR` | `127.0.0.1:8730` | The JSON-RPC WebSocket listener |
| `SOLWEAR_HTTP_ADDR` | `127.0.0.1:8731` | The static asset listener |
| `SOLWEAR_DATA_DIR` | `/var/lib/solwear` | Installed apps, the keystore, `runtime.json` |
| `SOLWEAR_SHELL_DIR` | `/usr/share/solwear/shell` | The built shell to serve |
| `SOLWEAR_HAL` | `pi` on Linux, otherwise `mock` | Which HAL implementation to load |

Port `0` in either address asks the operating system for a free port, so two
daemons never collide. Nothing then has to guess which port was chosen:

- `GET /system.json` on the asset port answers with `rpcUrl`, `httpUrl`,
  `rpcAddr`, `httpAddr` and the daemon version. The shell reads it on startup
  rather than assuming `8730`, and the host emulator answers the same document
  so the shell behaves identically in both.
- `<data_dir>/runtime.json` holds the same fields, written once both listeners
  are up and removed on shutdown. A supervisor or a test harness reads it
  instead of scraping the log.

```json
{
  "version": "0.1.0",
  "pid": 41234,
  "rpcAddr": "127.0.0.1:8730",
  "httpAddr": "127.0.0.1:8731",
  "rpcUrl": "ws://127.0.0.1:8730/rpc",
  "httpUrl": "http://127.0.0.1:8731/"
}
```

Every example below shows the JSON on the wire. The SDK call that produces it is
noted alongside.

### Error codes

| Code | Meaning |
| --- | --- |
| `-32700` | Parse error. The payload was not valid JSON. |
| `-32600` | Invalid request. Not a well-formed JSON-RPC 2.0 request object. |
| `-32601` | Method not found. |
| `-32602` | Invalid params. A required parameter is missing or of the wrong type. |
| `-32603` | Internal error. |
| `-32001` | **Capability denied.** The calling app did not declare this method's capability in its manifest. |

A capability denial looks like this:

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

---

## `system.info`

Device identity, the OS version, and the screen geometry. This is the call every
adaptive layout starts from.

**Capability:** `system` · **Parameters:** none

Request:

```json
{ "jsonrpc": "2.0", "id": 1, "method": "system.info", "params": {} }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "version": "0.1.0",
    "device": "Raspberry Pi 4 Model B",
    "screen": { "width": 480, "height": 480, "shape": "round" }
  }
}
```

`screen.shape` is `"round"` or `"square"`. Screens range from 240x240 to
800x480; nothing may assume a fixed size. In the SDK the same values are
available synchronously as `solwear.system.screen`, so layout code can run
before the first paint.

```ts
const info = await solwear.system.info();
const screen = solwear.system.screen;   // synchronous, same values
```

---

## `system.time`

The current time and the configured timezone.

**Capability:** `system` · **Parameters:** none

Request:

```json
{ "jsonrpc": "2.0", "id": 2, "method": "system.time", "params": {} }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "epochMs": 1772539200000,
    "timezone": "Europe/Kyiv"
  }
}
```

`epochMs` is milliseconds since the Unix epoch, UTC. `timezone` is an IANA zone
name. Use the `tick` event rather than polling this once a second: `tick`
carries the same `epochMs` and is throttled by the system when the display is
off.

```ts
const { epochMs, timezone } = await solwear.system.time();
```

---

## `system.stats`

Runtime and Linux resource counters used by the Stats app and developer tools.

**Capability:** `system` · **Parameters:** none

```json
{ "jsonrpc": "2.0", "id": 3, "method": "system.stats", "params": {} }
```

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "uptimeMs": 48320,
    "platform": { "os": "linux", "arch": "aarch64" },
    "memory": { "totalBytes": 1048576000, "availableBytes": 734003200, "processBytes": 12582912 },
    "storage": { "totalBytes": 4294967296, "availableBytes": 3019898880 },
    "load": { "one": 0.12, "five": 0.08, "fifteen": 0.03 },
    "apps": 5,
    "notifications": 1,
    "shellConnected": true
  }
}
```

The QEMU guest returns genuine Linux `/proc` and filesystem values. The fast
host emulator returns protocol-compatible process/mock values.

```ts
const stats = await solwear.system.stats();
```

---

## `power.status`

Battery level and charge state.

**Capability:** `power` · **Parameters:** none

Request:

```json
{ "jsonrpc": "2.0", "id": 3, "method": "power.status", "params": {} }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "percent": 68,
    "charging": false,
    "estimateMinutes": 412
  }
}
```

`percent` is an integer from 0 to 100. `estimateMinutes` is the estimated time
to empty when discharging, or to full when charging; it is a genuine estimate
and will move around, so present it as approximate. Under `MockHal` all three
values are scriptable, which is how the battery-warning path is tested.

```ts
const { percent, charging, estimateMinutes } = await solwear.power.status();
```

---

## `display.setBrightness`

Set the backlight level.

**Capability:** `display`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `percent` | number | yes | Target brightness, 0 to 100. |

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "display.setBrightness",
  "params": { "percent": 60 }
}
```

Response:

```json
{ "jsonrpc": "2.0", "id": 4, "result": {} }
```

On hardware this writes through `/sys/class/backlight`. A value outside 0 to 100
is `-32602`. Note that 0 means the dimmest the panel supports, which on some
panels is not fully off.

```ts
await solwear.display.setBrightness({ percent: 60 });
```

---

## `sensors.read`

Take a single reading from a named sensor.

**Capability:** `sensors`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `sensor` | string | yes | Sensor name, for example `heartRate`, `accelerometer`, `ambientLight`, `temperature`. |

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "sensors.read",
  "params": { "sensor": "heartRate" }
}
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "result": {
    "sensor": "heartRate",
    "value": 72,
    "unit": "bpm",
    "timestampMs": 1772539201337
  }
}
```

The result echoes the sensor name so a client batching several reads can match
responses without tracking request ids. `unit` is the physical unit of `value`.
An unknown sensor name is `-32602`.

Sensors are read over I2C on hardware and scripted from a JSON file under
`MockHal`, so a test can drive a heart rate curve deterministically.

```ts
const reading = await solwear.sensors.read({ sensor: "heartRate" });
```

---

## `notifications.list`

Every notification currently in the tray.

**Capability:** `notifications` · **Parameters:** none

Request:

```json
{ "jsonrpc": "2.0", "id": 6, "method": "notifications.list", "params": {} }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "result": {
    "items": [
      {
        "id": "ntf_01HQ8Z2",
        "title": "Transaction signed",
        "body": "0.25 SOL to 7xKX…9dPq",
        "appId": "tech.solwear.signer",
        "timestampMs": 1772539180000
      },
      {
        "id": "ntf_01HQ8Z1",
        "title": "Battery low",
        "body": "15% remaining",
        "appId": "tech.solwear.shell",
        "timestampMs": 1772538000000
      }
    ]
  }
}
```

Newest first. The list is the shell's tray, not a per-app inbox: an app with the
`notifications` capability sees every notification on the device.

```ts
const { items } = await solwear.notifications.list();
```

---

## `notifications.post`

Post a notification to the tray.

**Capability:** `notifications`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `title` | string | yes | Short headline. |
| `body` | string | yes | One or two lines of detail. |
| `appId` | string | yes | The posting app's id. Must match the caller. |

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "method": "notifications.post",
  "params": {
    "title": "Timer finished",
    "body": "25 minutes elapsed",
    "appId": "tech.example.timer"
  }
}
```

Response:

```json
{ "jsonrpc": "2.0", "id": 7, "result": { "id": "ntf_01HQ8Z3" } }
```

`appId` must be the calling app's own id; posting on behalf of another app is
rejected. Keep `title` under about twenty characters and `body` under about
sixty, or the tray will truncate them on a 240-pixel screen.

```ts
const { id } = await solwear.notifications.post({
  title: "Timer finished",
  body: "25 minutes elapsed",
  appId: "tech.example.timer",
});
```

---

## `apps.list`

Every installed app.

**Capability:** `apps` · **Parameters:** none

Request:

```json
{ "jsonrpc": "2.0", "id": 8, "method": "apps.list", "params": {} }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 8,
  "result": {
    "apps": [
      {
        "id": "tech.solwear.watchface",
        "name": "Classic Watchface",
        "version": "1.0.0",
        "type": "watchface",
        "capabilities": ["system", "power"],
        "author": "SolWear",
        "signed": true
      },
      {
        "id": "tech.solwear.signer",
        "name": "Solana Signer",
        "version": "1.0.0",
        "type": "app",
        "capabilities": ["system", "wallet", "notifications"],
        "author": "SolWear",
        "signed": true
      }
    ]
  }
}
```

Each entry is an `AppRecord`: the identity and capability fields from the
installed manifest, plus `signed`, which reports whether the package carried a
verified `signature.json`. A sideloaded package shows `"signed": false`.

```ts
const { apps } = await solwear.apps.list();
```

---

## `apps.install`

Install an app package.

**Capability:** `apps`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `source` | string | yes | URL or local path of the `.swa` package. |
| `expectedSha256` | string | no | Lowercase SHA-256 of the complete `.swa` archive. Required for store installs. |
| `expectedPublisherKey` | string | no | Canonical Base64 raw 32-byte Ed25519 public key. Required for store installs. |

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 9,
  "method": "apps.install",
  "params": {
    "source": "https://store.solwear.tech/pkg/tech.example.timer-1.2.0.swa",
    "expectedSha256": "1a8e08d2ae…",
    "expectedPublisherKey": "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE="
  }
}
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 9,
  "result": { "appId": "tech.example.timer", "version": "1.2.0" }
}
```

The daemon fetches the package, validates the manifest, and verifies any
embedded Ed25519 signature before writing files. When the integrity pins are
provided it also requires the complete archive hash and signing key to match;
the store app always supplies both registry values. Omitting the pins is only
for developer sideloading, where unsigned packages are intentionally allowed.
See
[Publishing to the Store](publishing-to-the-store.md) and
[Package Format and Signing](package-format-and-signing.md).

```ts
const { appId, version } = await solwear.apps.install(url, {
  expectedSha256: entry.sha256,
  expectedPublisherKey: entry.publisherKey,
});
```

---

## `apps.uninstall`

Remove an installed app.

**Capability:** `apps`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `appId` | string | yes | The app to remove. |

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 10,
  "method": "apps.uninstall",
  "params": { "appId": "tech.example.timer" }
}
```

Response:

```json
{ "jsonrpc": "2.0", "id": 10, "result": {} }
```

Removing an app that is not installed is `-32602`. System apps cannot be
uninstalled.

```ts
await solwear.apps.uninstall({ appId: "tech.example.timer" });
```

---

## `apps.launch`

Bring an app to the foreground.

**Capability:** `apps`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `appId` | string | yes | The app to launch. |

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 11,
  "method": "apps.launch",
  "params": { "appId": "tech.solwear.signer" }
}
```

Response:

```json
{ "jsonrpc": "2.0", "id": 11, "result": {} }
```

The result returns as soon as the shell has accepted the request, not when the
app has finished rendering. The app being replaced receives a `visibility` event
with `visible: false`.

```ts
await solwear.apps.launch({ appId: "tech.solwear.signer" });
```

---

## Wallet state and encryption

All wallet methods require the `wallet` capability. `wallet.status` reports
`onboarded`, `locked`, `protected`, `name` and `publicKey`.

```json
{ "jsonrpc": "2.0", "id": 12, "method": "wallet.status", "params": {} }
```

```json
{
  "jsonrpc": "2.0",
  "id": 12,
  "result": { "onboarded": true, "locked": false, "protected": true, "name": "My SolWear", "publicKey": "7xKXtg2CW87d97TXJSDpbD5jBkheTqA83TZRuJosgAsU" }
}
```

`wallet.setPassphrase` encrypts the key at rest with Argon2id and
ChaCha20-Poly1305. Passphrases must contain at least eight characters. An
encrypted wallet starts locked after a daemon restart.

```ts
await solwear.wallet.setPassphrase("correct horse battery staple", "My SolWear");
await solwear.wallet.lock();
await solwear.wallet.unlock("correct horse battery staple");
const status = await solwear.wallet.status();
const history = await solwear.wallet.activity();
```

`wallet.activity` returns recent successful signatures; it contains digests
and labels, never private keys or signed message contents.

---

## `wallet.publicKey`

The device's Solana public key.

**Capability:** `wallet` · **Parameters:** none

Request:

```json
{ "jsonrpc": "2.0", "id": 12, "method": "wallet.publicKey", "params": {} }
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 12,
  "result": { "publicKey": "7xKXtg2CW87d97TXJSDpbD5jBkheTqA83TZRuJosgAsU" }
}
```

Base58, as Solana public keys are written everywhere else. This is the only
piece of the keystore that ever crosses the API boundary. The private key never
leaves `solweard` and is never exposed over the API under any circumstances.

```ts
const { publicKey } = await solwear.wallet.publicKey();
```

---

## `wallet.signTransaction`

Sign a transaction with the device key, after the user confirms on the device
screen.

**Capability:** `wallet`

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `appId` | string | yes | The requesting app's id. Shown in the confirmation prompt. |
| `message` | string | yes | Base64-encoded Solana transaction message to sign. |

Request:

```json
{
  "jsonrpc": "2.0",
  "id": 13,
  "method": "wallet.signTransaction",
  "params": {
    "appId": "tech.solwear.signer",
    "message": "AQABA0dGVkNVU1RPTUVSAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
  }
}
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 13,
  "result": {
    "signature": "5VfydnLu4XwV2FGtLhFbYbmvPBRPBRwXCXbUpvRvVBFMbjJPUJvXvJ7d3aVhsPHtLQ4kt4RUwBLtQqJdCYqW9Xkp"
  }
}
```

This call **always** raises a confirmation prompt on the device screen showing
the requesting app and the decoded transaction, and it **never** signs without
an affirmative user action. There is no flag, no setting and no development mode
that bypasses it. That property is the reason SolWear exists, and it is not
negotiable.

The call blocks until the user responds. If the user declines, or the prompt
times out, the call returns an error rather than a signature:

```json
{
  "jsonrpc": "2.0",
  "id": 13,
  "error": { "code": -32603, "message": "user declined" }
}
```

Handle that path. A user declining is a normal outcome, not an exception.

```ts
try {
  const { signature } = await solwear.wallet.signTransaction({
    appId: "tech.solwear.signer",
    message: base64Message,
  });
} catch (err) {
  // The user declined, or the prompt timed out.
}
```

---

## NFC wallet exchange

The `nfc` capability exposes the ESP32-compatible wallet-record contract:

| Method | Parameters | Result |
| --- | --- | --- |
| `nfc.status` | none | `{ available, ready, enabled, backend, mode, detail? }` |
| `nfc.setEnabled` | `{ enabled: boolean }` | `{}` |
| `nfc.walletRecord` | none | external type and wallet JSON payload |
| `nfc.diagnostics` | none | status, I2C device, address and protocol |

```json
{
  "jsonrpc": "2.0",
  "id": 20,
  "result": {
    "externalType": "solwear:wallet",
    "payload": { "version": 1, "pubkey": "7xKXtg2CW87d97TXJSDpbD5jBkheTqA83TZRuJosgAsU", "network": "devnet" }
  }
}
```

```ts
const status = await solwear.nfc.status();
if (status.ready) await solwear.nfc.setEnabled(true);
const record = await solwear.nfc.walletRecord();
```

The host emulator implements the full mock contract. On Pi, arming fails with a
HAL-unavailable error until `/dev/i2c-1` and the PN532 Type 4 worker are both
present. See the [migration ledger](../LEGACY_MIGRATION.md).

---

## Events

Events are pushed to the client as JSON-RPC notifications — a request object
with no `id`, which therefore expects no response. The SDK surfaces them through
`solwear.on(...)`.

| Event | Payload | Fires |
| --- | --- | --- |
| `tick` | `{ epochMs }` | Once per second, throttled while the display is off. |
| `visibility` | `{ visible }` | When the app moves to or from the foreground. |
| `button` | `{ button, action }` | On a hardware button press or release. |
| `gesture` | `{ gesture, direction }` | On a recognised touch gesture. |

```json
{
  "jsonrpc": "2.0",
  "method": "event",
  "params": { "event": "tick", "data": { "epochMs": 1772539202000 } }
}
```

```ts
solwear.on("tick", ({ epochMs }) => render(epochMs));
solwear.on("visibility", ({ visible }) => (visible ? resume() : pause()));
solwear.on("button", ({ button, action }) => { /* ... */ });
solwear.on("gesture", ({ gesture, direction }) => { /* ... */ });
```

---

## Calling the API directly

Useful while debugging the daemon. Start it against the mock HAL and talk to the
socket yourself:

```bash
SOLWEAR_HAL=mock cargo run --manifest-path os/solweard/Cargo.toml
```

```bash
npx wscat -c ws://127.0.0.1:8730
> {"jsonrpc":"2.0","id":1,"method":"system.info","params":{}}
< {"jsonrpc":"2.0","id":1,"result":{"version":"0.1.0","device":"MockHal", ...}}
```

A direct socket client is not subject to the capability gate, because the gate
is applied to app calls arriving through the shell's bridge. That is a debugging
convenience on a loopback socket, not a security boundary you should rely on —
see [Capabilities and Security](capabilities-and-security.md).
