# SolWear QEMU emulator

This is the full-system, slow emulator. It boots a real ARM64 Debian Linux VM,
runs `solweard` under systemd, and forwards host ports 8730/8731 to the guest.
The shell and first-party apps are the same artifacts installed on Raspberry Pi.

The VM disk is intentionally different from the Raspberry Pi image. Pi OS
boots through Pi firmware and a Pi device tree; QEMU `virt` boots through UEFI
and virtio. Treating those as one image produced an emulator that could never
actually boot.

```sh
brew install qemu
./build-image.sh
./run.sh
```

After the guest reports that cloud-init is ready, verify the whole forwarded
stack from another terminal:

```sh
node ./smoke.mjs 8731 8730
```

The first build downloads Debian's official ARM64 generic cloud image and
creates a NoCloud seed containing the current daemon, shell and apps. Later
builds reuse the cached base. Use `--dry-run` to inspect the complete launch
command without starting the VM, `--headless` for CI, and `--no-browser` when
you only need the serial console.

The browser URL includes the forwarded RPC socket, so custom pairs work:

```sh
./run.sh --port 18741 --rpc-port 18740 --ssh-port 2223
```
