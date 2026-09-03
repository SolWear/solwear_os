# Using the Emulator

SolWear ships two emulators, and they are for different jobs.

| | Host simulator | QEMU emulator |
| --- | --- | --- |
| Path | `emulator/host` | `emulator/qemu` |
| What it runs | The real shell and app bundles in a desktop window | A real ARM64 Debian VM with the production daemon |
| Backed by | Protocol-compatible JavaScript daemon + `MockHal` | Production Rust `solweard` + `MockHal` |
| Start time | Under two seconds | Tens of seconds |
| Use it for | Everything, all day | Validating an image before you flash it |

You will spend almost all of your time in the host simulator. Reach for QEMU
when you have changed the image, the systemd units, or the daemon's interaction
with the real system, and you want to see the thing actually boot.

## The host simulator

```bash
solwear run
```

From inside an app directory this builds the app, starts the fast
protocol-compatible daemon with `MockHal`, and opens a desktop window rendering
the real system shell with your app loaded inside it.

This is not a mock of the shell. It is the shell — the same TypeScript that runs
on the device, talking the same JSON-RPC over the same WebSocket. The lightweight
daemon is an independently tested protocol twin; use QEMU when behavior must be
checked against the production Rust daemon and systemd.
That is the direct payoff of the decision to make the shell and every app
ordinary web content.

The window draws the device bezel, so a round screen is visibly round and you
can see at a glance when content lands in the clipped corners.

### Device profiles

Profiles are JSON files describing the screen, the buttons and the mock sensor
behaviour. Three ship, and all three are ship-blocking: a layout change is not
finished until it has been looked at on each.

| Profile | Screen | Shape | Notes |
| --- | --- | --- | --- |
| `pi-round-480` | 480x480 | round | The default. Clipped corners; the strictest layout test. |
| `pi-square-320` | 320x320 | square | The small end. Type and spacing get tight here. |
| `pi-wide-800x480` | 800x480 | square | Landscape. Catches layouts that assume a square. |

```bash
solwear run --profile pi-round-480
solwear run --profile pi-square-320
solwear run --profile pi-wide-800x480
```

A profile looks like this:

```json
{
  "id": "pi-round-480",
  "name": "Pi Round 480",
  "screen": { "width": 480, "height": 480, "shape": "round" },
  "buttons": [
    { "id": "side", "label": "Side button" },
    { "id": "crown", "label": "Crown" }
  ],
  "sensors": {
    "heartRate": { "unit": "bpm", "script": "sensors/resting-heart-rate.json" },
    "ambientLight": { "unit": "lux", "value": 320 }
  },
  "power": { "percent": 68, "charging": false, "estimateMinutes": 412 }
}
```

Copy one, change the numbers, and pass the path to test a screen we do not ship
a profile for:

```bash
solwear run --profile ./profiles/my-240-round.json
```

Anything from 240x240 to 800x480 must render correctly. If a layout only works
on one profile, it is not adaptive, it is lucky.

### Scripting the mock hardware

`MockHal` is deterministic, and every value it returns can be scripted from a
JSON file. That is what makes the awkward paths testable: a battery that falls
below the warning threshold, a heart rate that spikes, a clock that crosses
midnight.

```json
{
  "power": [
    { "atMs": 0,      "percent": 20, "charging": false },
    { "atMs": 30000,  "percent": 15, "charging": false },
    { "atMs": 60000,  "percent": 15, "charging": true }
  ],
  "sensors": {
    "heartRate": [
      { "atMs": 0,     "value": 64 },
      { "atMs": 10000, "value": 142 },
      { "atMs": 40000, "value": 88 }
    ]
  },
  "time": { "startEpochMs": 1772539200000, "rate": 1.0 }
}
```

```bash
solwear run --profile pi-round-480 --hal-script ./test/low-battery.json
```

Every HAL method works under `MockHal`. A method that only exists on real
hardware is a bug, not a limitation — if you find one, report it.

### Buttons and gestures

The simulator window has controls for the buttons the profile declares, and the
window accepts drag gestures. Both are delivered to your app as the `button` and
`gesture` events, identically to hardware.

### Developer cockpit

The right-hand panel is intentionally outside the emulated watch screen. It
shows requests, errors, uptime and live RPC activity, and lets a developer set
battery, charging, brightness, steps, heart rate, temperature and ambient light
without editing a fixture or restarting. It can also inject notifications.
These controls mutate only `MockHal`; app code still receives values through
the normal capability-gated API.

### Running the shell without an app

From the repository root:

```bash
npm --prefix emulator/host start -- --profile pi-round-480
```

This starts the shell with the first-party apps installed and no app of yours
loaded. Useful when working on the shell itself rather than on an app.

## The QEMU emulator

```bash
./emulator/qemu/build-image.sh
solwear run --qemu
```

This boots an official Debian Bookworm ARM64 cloud image with
`qemu-system-aarch64`, then starts the production static `solweard` binary under
systemd. HTTP, JSON-RPC and SSH are forwarded to the host. It is a real Linux
VM: `/proc`, filesystem accounting, process RSS, load averages, systemd and the
aarch64 executable are genuine guest behavior.

It is deliberately not the byte-for-byte Raspberry Pi image. Pi OS boots via
Pi firmware and a Pi device tree, while QEMU's portable `virt` machine boots via
UEFI and virtio. The VM validates Linux/ARM64, the daemon, package installation
and boot ordering; only a physical Pi validates its display, I2C, NFC, battery
and backlight drivers. The browser stays on the host and renders the shell
served by the guest, which makes headless automation reliable.

Use it before you flash hardware, and in CI when the daemon or VM build changes.

### Missing QEMU

If `qemu-system-aarch64` is not installed, the emulator says so and prints the
exact command to fix it, rather than failing with a stack trace:

```
solwear run --qemu
  qemu-system-aarch64 was not found on your PATH.

  macOS:          brew install qemu
  Debian/Ubuntu:  sudo apt install qemu-system-arm
  Fedora:         sudo dnf install qemu-system-aarch64

  The host simulator does not need QEMU. Run `solwear run` instead.
```

`solwear doctor` reports QEMU as an optional dependency for the same reason: you
can develop apps indefinitely without it.

### Installing into QEMU

The SSH port is forwarded to `2222` by default:

```bash
solwear package
solwear install --device 127.0.0.1 --port 2222
```

That exercises the real verification path — SHA-256, Ed25519, manifest
validation — against the real daemon, which is exactly what you want to check
before trusting hardware.

## Choosing between them

Reach for the host simulator when you are writing an app, changing shell
layout, testing adaptive behaviour across profiles, or driving a scripted HAL
scenario. Reach for QEMU when you have touched the image, the systemd units, the
boot order or the real install path, or when you are about to flash a device.

## Next

- [Your First Watchface](your-first-watchface.md) — build something to run in it.
- [Flashing a Raspberry Pi](flashing-a-raspberry-pi.md) — when the emulator is
  no longer enough.
