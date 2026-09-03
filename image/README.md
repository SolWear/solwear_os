# Building a SolWear OS image

There are two scripts here. `build-image.sh` turns a stock Raspberry Pi OS Lite
image into a SolWear OS image. `build-on-pi.sh` is the whole path around it:
toolchain, builds, stock image download, and then `build-image.sh`.

## Why this runs on a Pi and not on a Mac

`build-image.sh` edits the image through a loop device, which needs `losetup`,
`mount` and friends. macOS has none of them, so the image has to be built on a
Linux machine. A Raspberry Pi running Raspberry Pi OS is the most convenient
one, because it is also arm64, so the daemon is compiled natively rather than
cross-compiled.

Any arm64 Linux machine works. On an x86 Linux machine you additionally need
the aarch64 Rust target and a cross linker, and you would pass the
cross-compiled binary with `--solweard`.

## The whole build, on the Pi

Clone the repository on the Pi and run one command:

```sh
git clone https://github.com/SolWear/solwear_os.git
cd solwear_os
./image/build-on-pi.sh --wifi-ssid HomeNet --wifi-psk secret --wifi-country UA
```

The repository is private, so `git clone` will ask for credentials. A GitHub
personal access token with `repo` scope works as the password, or install the
`gh` CLI and run `gh auth login` first.

Add `--screen WxH:shape` when the panel is not the 480x480 round default, for
example `--screen 800x480:rect`. `--skip-deps` skips the toolchain install on a
second run.

The script needs about 8 GB free and takes roughly 20 minutes on a Pi 4, most
of it compiling the daemon and downloading the stock image. Both are cached:
a second run is much faster.

The result is `image/build/solwear-os.img`.

## Flashing

Use Raspberry Pi Imager, or write it directly:

```sh
sudo dd if=image/build/solwear-os.img of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with the SD card device. Check it with `lsblk` first: `dd`
writes to whatever you name, including the disk you are running from.

## What ends up on the card

| Path | What it is |
| --- | --- |
| `/usr/bin/solweard` | the system daemon |
| `/usr/share/solwear/shell/` | the built shell bundle |
| `/usr/share/solwear/apps/` | preinstalled `.swa` packages |
| `/etc/systemd/system/solweard.service` | the daemon unit |
| `/etc/systemd/system/solwear-ui.service` | cage plus Chromium in kiosk mode |
| `/etc/systemd/system/solwear-firstboot.service` | installs cage and Chromium on first boot |

The first boot needs a network, because cage and Chromium are installed then
rather than baked in. It takes a few minutes, and the panel stays dark until it
finishes.

## Watching it come up

SSH in, or use a keyboard on the Pi:

```sh
journalctl -u solwear-firstboot -f    # first boot only
journalctl -u solweard -u solwear-ui -f
```

The daemon serves the shell on `http://127.0.0.1:8731/` and the JSON-RPC API on
`ws://127.0.0.1:8730/`. Both are bound to loopback, so to look at the shell from
another machine, forward the port over SSH:

```sh
ssh -L 8731:127.0.0.1:8731 -L 8730:127.0.0.1:8730 pi@solwear.local
```

## Building only the pieces

`build-image.sh` takes each part separately, so you can rebuild one of them and
reuse the rest:

```sh
sudo ./image/build-image.sh \
  --image raspios-lite-arm64.img.xz \
  --output solwear-os.img \
  --solweard ../os/solweard/target/release/solweard \
  --shell ../os/shell/dist \
  --apps ./build/apps \
  --screen 480x480:round
```

Run it with `--help` for every option. It checks all its requirements before it
copies anything, and removes a partial output on failure, so an interrupted run
never leaves a half-written image behind.
