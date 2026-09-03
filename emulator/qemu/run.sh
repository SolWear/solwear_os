#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IMAGE="${SOLWEAR_IMAGE:-$SCRIPT_DIR/build/solwear-linux-arm64.qcow2}"
SEED=""
HTTP_PORT=8731
RPC_PORT=8730
SSH_PORT=2222
MEMORY=1024
DRY_RUN=0
HEADLESS=0
OPEN_BROWSER=1

while [ "$#" -gt 0 ]; do
  case "$1" in
    --image) IMAGE=$2; shift 2 ;;
    --seed) SEED=$2; shift 2 ;;
    --port|--http-port) HTTP_PORT=$2; shift 2 ;;
    --rpc-port) RPC_PORT=$2; shift 2 ;;
    --ssh-port) SSH_PORT=$2; shift 2 ;;
    --memory) MEMORY=$2; shift 2 ;;
    --headless) HEADLESS=1; shift ;;
    --no-browser) OPEN_BROWSER=0; shift ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h)
      echo "usage: run.sh [--image FILE] [--seed FILE] [--port HTTP_PORT] [--rpc-port RPC_PORT] [--ssh-port PORT] [--memory MB] [--headless] [--no-browser] [--dry-run]"
      exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ ! -f "$IMAGE" ]; then
  echo "SolWear image not found: $IMAGE" >&2
  echo "Build one under image/, or pass --image /path/to/solwear-aarch64.img" >&2
  exit 1
fi
if [ -z "$SEED" ]; then
  SEED="${IMAGE%.*}-seed.iso"
fi
if [ ! -f "$SEED" ]; then
  echo "SolWear NoCloud seed not found: $SEED" >&2
  echo "Build the VM with emulator/qemu/build-image.sh, or pass --seed FILE" >&2
  exit 1
fi
if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "qemu-system-aarch64 is not installed (macOS: brew install qemu)" >&2
  exit 1
fi

FIRMWARE=${SOLWEAR_QEMU_FIRMWARE:-/opt/homebrew/share/qemu/edk2-aarch64-code.fd}
if [ ! -f "$FIRMWARE" ]; then
  echo "AArch64 UEFI firmware not found: $FIRMWARE" >&2
  echo "Set SOLWEAR_QEMU_FIRMWARE to edk2-aarch64-code.fd" >&2
  exit 1
fi

set -- qemu-system-aarch64 \
  -machine virt \
  -accel hvf \
  -cpu host \
  -smp 4 \
  -m "$MEMORY" \
  -bios "$FIRMWARE" \
  -drive "if=virtio,format=qcow2,file=$IMAGE" \
  -drive "if=virtio,format=raw,readonly=on,file=$SEED" \
  -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:$HTTP_PORT-10.0.2.15:8731,hostfwd=tcp:127.0.0.1:$RPC_PORT-10.0.2.15:8730,hostfwd=tcp:127.0.0.1:$SSH_PORT-10.0.2.15:22" \
  -serial mon:stdio

if [ "$HEADLESS" -eq 1 ]; then
  set -- "$@" -display none
else
  set -- "$@" -device virtio-gpu-pci -device qemu-xhci -device usb-kbd -device usb-tablet
fi

if [ "$DRY_RUN" -eq 1 ]; then
  printf '%s\n' "$*"
  exit 0
fi

if [ "$OPEN_BROWSER" -eq 1 ]; then
  URL="http://127.0.0.1:$HTTP_PORT/?rpc=ws://127.0.0.1:$RPC_PORT/rpc"
  (
    attempts=0
    while [ "$attempts" -lt 180 ]; do
      if curl -fsS "http://127.0.0.1:$HTTP_PORT/system.json" >/dev/null 2>&1; then
        open "$URL"
        exit 0
      fi
      attempts=$((attempts + 1))
      sleep 1
    done
  ) &
fi

exec "$@"
