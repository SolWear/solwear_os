# SolWear QEMU emulator

This is the full-system, slow emulator. It boots the same raw aarch64 image
used by the device and forwards host ports 8730/8731 to `solweard` in the guest.

```sh
brew install qemu
./run.sh --image ../../image/dist/solwear-aarch64.img
```

The image must support QEMU's `virt` machine (virtio block, network and GPU).
Use `--dry-run` to inspect the complete command without starting the VM.
