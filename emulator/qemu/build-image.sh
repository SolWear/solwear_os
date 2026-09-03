#!/usr/bin/env bash
# Build a real ARM64 Linux VM for the SolWear full-system emulator.
#
# This is deliberately separate from image/build-image.sh.  Raspberry Pi OS
# images target Pi firmware and a Pi device tree; QEMU's portable `virt`
# machine uses UEFI and virtio.  Both images carry the same solweard, shell and
# app bundles, but each keeps the boot contract appropriate to its hardware.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="$HERE/build"
CACHE="$HERE/cache"
OUTPUT="$BUILD/solwear-linux-arm64.qcow2"
SEED="$BUILD/solwear-linux-arm64-seed.iso"
BASE_URL="https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-arm64.qcow2"
FORCE=0
SKIP_BUILD=0

usage() {
  cat <<'EOF'
usage: build-image.sh [--output FILE] [--base-url URL] [--force] [--skip-build]

Creates a QEMU `virt` compatible ARM64 Debian image and its NoCloud seed ISO.
The default output is emulator/qemu/build/solwear-linux-arm64.qcow2.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --output) OUTPUT="$2"; SEED="${2%.*}-seed.iso"; shift 2 ;;
    --base-url) BASE_URL="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    --skip-build) SKIP_BUILD=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for tool in curl qemu-img hdiutil npm cargo rustup tar base64 grep find install cp tr mv chmod; do
  command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 1; }
done

if [[ -e "$OUTPUT" || -e "$SEED" ]]; then
  [[ $FORCE -eq 1 ]] || { echo "output exists; pass --force to rebuild: $OUTPUT" >&2; exit 1; }
  rm -f -- "$OUTPUT" "$SEED"
fi

mkdir -p "$BUILD" "$CACHE"
BASE="$CACHE/$(basename "${BASE_URL%%\?*}")"
if [[ ! -s "$BASE" ]]; then
  echo "==> downloading official Debian ARM64 cloud image"
  curl --fail --location --progress-bar "$BASE_URL" --output "$BASE.part"
  mv "$BASE.part" "$BASE"
fi

if [[ $SKIP_BUILD -eq 0 ]]; then
  echo "==> building SDK, CLI, shell and first-party apps"
  npm --prefix "$ROOT/sdk/runtime" run build
  npm --prefix "$ROOT/sdk/cli" run build
  npm --prefix "$ROOT/os/shell" run build
  for app in watchface signer store stats games; do
    npm --prefix "$ROOT/apps/$app" run build
    npm --prefix "$ROOT/apps/$app" run package
  done

  if ! rustup target list --installed | grep -qx aarch64-unknown-linux-musl; then
    echo "==> installing the static ARM64 Rust target"
    rustup target add aarch64-unknown-linux-musl
  fi
  echo "==> building static ARM64 solweard"
  CARGO_TARGET_AARCH64_UNKNOWN_LINUX_MUSL_LINKER=rust-lld \
    RUSTFLAGS='-C target-feature=+crt-static' \
    cargo build --release --target aarch64-unknown-linux-musl \
      --manifest-path "$ROOT/os/solweard/Cargo.toml"
fi

DAEMON="$ROOT/os/solweard/target/aarch64-unknown-linux-musl/release/solweard"
[[ -x "$DAEMON" ]] || { echo "missing ARM64 daemon: $DAEMON" >&2; exit 1; }
[[ -f "$ROOT/os/shell/dist/index.html" ]] || { echo "shell has not been built" >&2; exit 1; }

WORK="$(mktemp -d /tmp/solwear-qemu-build.XXXXXX)"
cleanup() { rm -rf -- "$WORK"; }
trap cleanup EXIT

mkdir -p "$WORK/payload/usr/bin" \
  "$WORK/payload/usr/share/solwear/shell" \
  "$WORK/payload/usr/share/solwear/packages" \
  "$WORK/payload/etc/solwear" \
  "$WORK/payload/etc/systemd/system/solweard.service.d"
install -m 0755 "$DAEMON" "$WORK/payload/usr/bin/solweard"
cp -a "$ROOT/os/shell/dist/." "$WORK/payload/usr/share/solwear/shell/"
for app in watchface signer store stats games; do
  package="$(find "$ROOT/apps/$app/dist" -maxdepth 1 -name '*.swa' -print -quit)"
  [[ -n "$package" ]] || { echo "no .swa package for $app" >&2; exit 1; }
  cp "$package" "$WORK/payload/usr/share/solwear/packages/"
done

cat > "$WORK/payload/etc/solwear/solweard.env" <<'EOF'
SOLWEAR_HAL=mock
SOLWEAR_SCREEN=480x480:round
SOLWEAR_DATA_DIR=/var/lib/solwear
SOLWEAR_SHELL_DIR=/usr/share/solwear/shell
SOLWEAR_RPC_ADDR=0.0.0.0:8730
SOLWEAR_HTTP_ADDR=0.0.0.0:8731
SOLWEAR_LOG=info
EOF
cp "$ROOT/image/systemd/solweard.service" "$WORK/payload/etc/systemd/system/solweard.service"
cat > "$WORK/payload/etc/systemd/system/solweard.service.d/qemu-console.conf" <<'EOF'
[Service]
StandardOutput=journal+console
StandardError=journal+console
EOF
# Do not embed macOS AppleDouble/provenance metadata in the Linux payload.
COPYFILE_DISABLE=1 tar --no-xattrs -C "$WORK/payload" -czf "$WORK/payload.tar.gz" .
PAYLOAD_B64="$(base64 < "$WORK/payload.tar.gz" | tr -d '\n')"

mkdir -p "$WORK/seed"
cat > "$WORK/seed/meta-data" <<'EOF'
instance-id: solwear-qemu-v1
local-hostname: solwear-qemu
EOF
cat > "$WORK/seed/user-data" <<EOF
#cloud-config
users:
  - default
  - name: solwear
    system: true
    shell: /usr/sbin/nologin
write_files:
  - path: /var/tmp/solwear-payload.tar.gz
    permissions: '0600'
    encoding: b64
    content: $PAYLOAD_B64
runcmd:
  - [mkdir, -p, /var/lib/solwear/apps]
  - [tar, -xzf, /var/tmp/solwear-payload.tar.gz, -C, /]
  - [chown, -R, solwear:solwear, /var/lib/solwear]
  - [chmod, '0600', /etc/solwear/solweard.env]
  - [systemctl, daemon-reload]
  - [systemctl, enable, solweard.service]
  - [/bin/sh, -c, 'for p in /usr/share/solwear/packages/*.swa; do SOLWEAR_DATA_DIR=/var/lib/solwear /usr/bin/solweard install "\$p" --allow-unsigned; done']
  - [chown, -R, solwear:solwear, /var/lib/solwear]
  - [systemctl, restart, solweard.service]
  - [rm, -f, /var/tmp/solwear-payload.tar.gz]
final_message: 'SolWear QEMU guest is ready'
EOF

echo "==> creating writable VM disk and NoCloud seed"
qemu-img convert -f qcow2 -O qcow2 "$BASE" "$OUTPUT"
qemu-img resize "$OUTPUT" 4G >/dev/null
hdiutil makehybrid -quiet -iso -joliet -default-volume-name cidata \
  -o "$SEED" "$WORK/seed"

echo ""
echo "Built real ARM64 Linux VM:"
echo "  disk: $OUTPUT"
echo "  seed: $SEED"
echo "Run it with:"
echo "  $HERE/run.sh --image '$OUTPUT'"
