# Flashing a Raspberry Pi

This page takes you from the repository to a Raspberry Pi booting straight into
the SolWear shell.

## What you need

| | |
| --- | --- |
| Board | Raspberry Pi 4 Model B or Raspberry Pi 5 |
| Storage | microSD card, 8 GB or larger, or a USB SSD |
| Display | Any DSI or HDMI panel from 240x240 to 800x480, round or square |
| Power | The supply your board actually wants — 5V/3A for a Pi 4, 5V/5A for a Pi 5 |
| Network | Wi-Fi credentials, or Ethernet for first boot |
| Host | macOS or Linux, with the repository built |

An underpowered supply is the single most common cause of a Pi that boots, shows
the shell, and then behaves strangely under load. Rule it out first.

## What the image contains

The build script starts from a stock Raspberry Pi OS Lite (arm64, Bookworm)
image and adds:

- the `solweard` binary, built for `aarch64-unknown-linux-gnu`;
- the shell's static assets;
- the preinstalled system apps — watchface, signer and store;
- a `solwear` system user that owns the runtime state;
- `solweard.service`, starting the daemon before the UI;
- `solwear-ui.service`, running `cage` with Chromium in kiosk mode against
  `http://127.0.0.1:8731`;
- a read-only root filesystem where practical;
- first-boot Wi-Fi provisioning.

There is no custom kernel and no Yocto layer. It is Raspberry Pi OS with our
software and our units on top, which is why it stays maintainable.

## Build the image

```bash
rustup target add aarch64-unknown-linux-gnu
cargo build --release --target aarch64-unknown-linux-gnu \
  --manifest-path os/solweard/Cargo.toml
npm --prefix os/shell run build
./image/build.sh --profile pi4 --out image/out/solwear-0.1.0-pi4.img
```

`--profile` is `pi4` or `pi5`. The script downloads the base image the first
time and caches it. Expect several minutes, mostly spent decompressing and
mounting.

Building on macOS uses a Linux container for the steps that need to mount an
ext4 filesystem; the script handles that and tells you if the container runtime
is missing.

Provision Wi-Fi at build time if you would rather not do it on first boot:

```bash
./image/build.sh --profile pi4 \
  --wifi-ssid "YourNetwork" --wifi-psk "yourpassword" \
  --out image/out/solwear-0.1.0-pi4.img
```

Credentials passed this way are written into the image. Do not share an image
built with them.

## Test the image before you flash it

The QEMU emulator boots the real image, and it is much cheaper than a flash and
a reboot cycle:

```bash
solwear run --qemu --image image/out/solwear-0.1.0-pi4.img
```

You are looking for three things: the daemon coming up before the UI, `cage` and
Chromium starting, and the shell rendering. If any of those fails here, it will
fail on hardware too. If `qemu-system-aarch64` is missing, the emulator prints
the exact install command for your platform.

## Flash it

The easiest route is [Raspberry Pi Imager](https://www.raspberrypi.com/software/):
choose **Use custom**, select the `.img`, choose your card, and write. Do not
apply Imager's OS customisation settings — the image already carries its own
user, services and provisioning, and Imager's customisation will fight them.

From the command line on macOS:

```bash
diskutil list                                  # identify the card, carefully
diskutil unmountDisk /dev/disk4
sudo dd if=image/out/solwear-0.1.0-pi4.img of=/dev/rdisk4 bs=4m status=progress
sync
diskutil eject /dev/disk4
```

On Linux:

```bash
lsblk                                          # identify the card, carefully
sudo umount /dev/sdX*
sudo dd if=image/out/solwear-0.1.0-pi4.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

`dd` writes to whatever you point it at without asking twice. Confirm the device
node before you press return, not after.

## First boot

Insert the card, connect the display, then power the board.

1. The image expands its filesystem and reboots once. This is normal.
2. `solweard` starts and begins serving on `127.0.0.1:8730` and
   `127.0.0.1:8731`.
3. `cage` starts Chromium in kiosk mode against the asset server.
4. The shell appears and picks up the screen geometry from `system.info`.

If Wi-Fi was not provisioned at build time, the device brings up a provisioning
screen with a short-lived access point; join it and enter the credentials there.

First boot takes a minute or two. Later boots reach the shell in a few seconds.

## Connect to it

The image advertises itself as `solwear.local`:

```bash
ssh solwear@solwear.local
```

Push an app straight from your working directory:

```bash
solwear install --device solwear.local
```

That runs the real verification path on the real daemon — SHA-256, Ed25519 where
a signature is present, manifest validation — before anything is written.

## Checking on it

```bash
systemctl status solweard
systemctl status solwear-ui
journalctl -u solweard -f
journalctl -u solwear-ui -f
```

The API is reachable from your machine over an SSH tunnel:

```bash
ssh -L 8730:127.0.0.1:8730 solwear@solwear.local
npx wscat -c ws://127.0.0.1:8730
> {"jsonrpc":"2.0","id":1,"method":"system.info","params":{}}
```

## When it does not work

**A black screen, but SSH works.** The daemon is up and the UI is not. Check
`journalctl -u solwear-ui`. Usually `cage` cannot open the display, which means
the panel's overlay is not configured in `/boot/firmware/config.txt`.

**The shell renders at the wrong size.** The shell takes its geometry from
`system.info`, which takes it from the panel. If the panel reports the wrong
resolution, fix the overlay rather than hardcoding anything in the shell.

**Content is clipped on a round screen.** The shape is not being detected as
round, so the safe inset is not applied. Check what `system.info` returns.

**The board reboots under load.** Power supply. Almost always power supply.

**The read-only root refuses a write.** That is intentional. For a temporary
change:

```bash
sudo mount -o remount,rw /
# ... make the change ...
sudo mount -o remount,ro /
```

Anything that needs to persist belongs in the image build, not in a hand edit
that the next flash will erase.

## Updating a device

Rebuild and reflash for anything touching the image or the daemon. For app-only
changes, `solwear install --device` is enough and takes seconds.

## Next

- [Using the Emulator](using-the-emulator.md) — validate before you flash.
- [Publishing to the Store](publishing-to-the-store.md) — ship to devices that
  are not yours.
