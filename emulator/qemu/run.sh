#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
IMAGE="${SOLWEAR_IMAGE:-$SCRIPT_DIR/../../image/dist/solwear-aarch64.img}"
HTTP_PORT=8731
RPC_PORT=8730
MEMORY=1024
DRY_RUN=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --image) IMAGE=$2; shift 2 ;;
    --port|--http-port) HTTP_PORT=$2; shift 2 ;;
    --rpc-port) RPC_PORT=$2; shift 2 ;;
    --memory) MEMORY=$2; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    --help|-h)
      echo "usage: run.sh [--image FILE] [--port HTTP_PORT] [--rpc-port RPC_PORT] [--memory MB] [--dry-run]"
      exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ ! -f "$IMAGE" ]; then
  echo "SolWear image not found: $IMAGE" >&2
  echo "Build one under image/, or pass --image /path/to/solwear-aarch64.img" >&2
  exit 1
fi
if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "qemu-system-aarch64 is not installed (macOS: brew install qemu)" >&2
  exit 1
fi

set -- qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -smp 4 \
  -m "$MEMORY" \
  -drive "if=virtio,format=raw,file=$IMAGE" \
  -device virtio-gpu-pci \
  -device qemu-xhci \
  -device usb-kbd \
  -device usb-tablet \
  -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:$HTTP_PORT-:8731,hostfwd=tcp:127.0.0.1:$RPC_PORT-:8730" \
  -serial mon:stdio

if [ "$DRY_RUN" -eq 1 ]; then
  printf '%s\n' "$*"
  exit 0
fi
exec "$@"
