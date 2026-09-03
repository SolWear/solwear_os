# ESP32 to Linux migration status

This is the parity ledger between the original ESP32-S3 firmware in
`SolWear/solwear` and the Raspberry Pi/Linux platform. “Emulated” means the
whole app/API flow is available without a Pi. It does not mean that QEMU can
electrically emulate a peripheral connected to GPIO or I2C.

| ESP32 feature | Linux implementation | Host emulator | QEMU ARM64 | Physical Pi |
| --- | --- | --- | --- | --- |
| GM, GN, Digital, Analog, Wallet and Minimal faces | `apps/watchface` | Ready | Ready | UI ready |
| Ping Pong, Tetris and Tamagotchi | `apps/games` | Ready | Ready | UI ready |
| Steps, heart rate and temperature views | `apps/stats` + sensor API | Scriptable | Mock HAL | Driver validation required |
| Battery and charging status | `power.status` | Live controls | Mock HAL | Gauge/sysfs validation required |
| Brightness | display API + Settings | Live control | Mock HAL | Backlight/sysfs validation required |
| App launcher, Store and notifications | shell, daemon and `.swa` lifecycle | Ready | Ready | Ready |
| Wallet public key and signing | encrypted Rust keystore + confirmation | Ready | Ready | Ready, user-flow check required |
| Wallet name, password, lock and unlock | Argon2id + ChaCha20-Poly1305 | Ready | Ready | Ready, reboot check required |
| Signing history | persistent `wallet-activity.json` | Ready | Ready | Ready |
| NFC wallet record | `nfc.walletRecord`, external type `solwear:wallet` | Ready | Ready | Payload ready |
| NFC arm/disarm and diagnostics | `nfc.status`, `nfc.setEnabled`, cockpit control | Ready | Mock HAL ready | PN532 worker required |
| PN532 Type 4 phone exchange and sign response | API boundary defined | Contract only | Not emulatable | **Not yet implemented/validated** |
| ESP32 serial diagnostics | structured daemon logs + emulator RPC log | Ready | Ready | Ready |

## What “real Linux emulator” means

`emulator/qemu` boots an official Debian Bookworm ARM64 guest through UEFI and
virtio. The aarch64 `solweard` binary is launched by systemd inside that guest,
and `/proc`, memory, load, storage, networking and package installation are real
Linux behavior. The browser remains on the Mac and connects to forwarded guest
ports.

The Raspberry Pi image is separate because a Pi boots using Raspberry Pi
firmware, its own device tree and board-specific drivers. QEMU therefore cannot
prove that the round display, battery gauge, buttons, I2C sensors or PN532 are
wired and configured correctly. Those final cells require the actual Pi.

## Remaining hardware gate

The largest open parity item is the PN532 Type 4 target-mode worker. The daemon
already emits the same wallet external-type payload as the old firmware:

```json
{
  "externalType": "solwear:wallet",
  "payload": { "version": 1, "pubkey": "<base58>", "network": "devnet" }
}
```

On a Pi, `nfc.status` checks `/dev/i2c-1` and the target-mode worker separately.
It will refuse to arm instead of claiming success when either is missing. The
worker must implement the old `solwear:sign_request` and
`solwear:sign_response` NDEF exchange, then be tested with the mobile app and
the actual PN532 before this ledger can mark NFC complete.
