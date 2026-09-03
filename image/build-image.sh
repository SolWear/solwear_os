#!/usr/bin/env bash
#
# Build a SolWear OS image from a stock Raspberry Pi OS Lite (arm64, Bookworm)
# image.
#
# The script never modifies the image you give it. It copies the source to the
# output path, then edits the copy through a loop device. Every requirement is
# checked before the copy starts, and any failure removes the partial output,
# so an interrupted run can never leave a half-modified image behind.
#
# What it installs:
#   * /usr/bin/solweard                      the system daemon (aarch64)
#   * /usr/share/solwear/shell/              the built shell bundle
#   * /usr/share/solwear/apps/               preinstalled .swa packages
#   * a `solwear` system user, via systemd-sysusers
#   * solweard.service and solwear-ui.service (cage + Chromium kiosk)
#   * solwear-firstboot.service, which installs cage and Chromium on first boot
#   * a NetworkManager profile for first-boot Wi-Fi
#
# Usage:
#   sudo ./build-image.sh \
#     --image raspios-lite-arm64.img.xz \
#     --output solwear-os.img \
#     --solweard ../os/solweard/target/aarch64-unknown-linux-gnu/release/solweard \
#     --shell ../os/shell/dist \
#     --wifi-ssid HomeNet --wifi-psk secret --wifi-country GB \
#     --screen 480x480:round

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMAGE=""
OUTPUT="solwear-os.img"
SOLWEARD_BIN="${HERE}/../os/solweard/target/aarch64-unknown-linux-gnu/release/solweard"
SHELL_DIST="${HERE}/../os/shell/dist"
APPS_DIR=""
WIFI_SSID=""
WIFI_PSK=""
WIFI_COUNTRY="GB"
HOSTNAME="solwear"
SCREEN="480x480:round"
GROW_MB=768
FORCE=0
ENABLE_SSH=1

die() {
  echo "error: $*" >&2
  exit 1
}

note() {
  echo "==> $*"
}

usage() {
  sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
  exit "${1:-0}"
}

# --- arguments -------------------------------------------------------------

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image) IMAGE="${2:-}"; shift 2 ;;
    --output) OUTPUT="${2:-}"; shift 2 ;;
    --solweard) SOLWEARD_BIN="${2:-}"; shift 2 ;;
    --shell) SHELL_DIST="${2:-}"; shift 2 ;;
    --apps) APPS_DIR="${2:-}"; shift 2 ;;
    --wifi-ssid) WIFI_SSID="${2:-}"; shift 2 ;;
    --wifi-psk) WIFI_PSK="${2:-}"; shift 2 ;;
    --wifi-country) WIFI_COUNTRY="${2:-}"; shift 2 ;;
    --hostname) HOSTNAME="${2:-}"; shift 2 ;;
    --screen) SCREEN="${2:-}"; shift 2 ;;
    --grow-mb) GROW_MB="${2:-}"; shift 2 ;;
    --no-ssh) ENABLE_SSH=0; shift ;;
    --force) FORCE=1; shift ;;
    -h|--help) usage 0 ;;
    *) echo "unknown option: $1" >&2; usage 1 ;;
  esac
done

# --- preflight -------------------------------------------------------------
#
# Everything that could stop the build is checked here, before a single byte is
# written. A failure in this section leaves the filesystem exactly as it was.

note "checking the build environment"

[[ "$(uname -s)" == "Linux" ]] || die "image customisation needs Linux loop devices.
  This machine is $(uname -s). Run the script on a Linux host, in a VM, or in a
  privileged container, for example:
    docker run --rm -it --privileged -v \"\$PWD\":/work -w /work debian:bookworm \\
      bash -c 'apt-get update && apt-get install -y file xz-utils parted e2fsprogs dosfstools && image/build-image.sh ...'"

if [[ $EUID -ne 0 ]]; then
  die "this script must run as root (loop mounts and file ownership). Re-run with sudo."
fi

missing=()
for tool in losetup mount umount mountpoint partprobe file install truncate sed awk \
  cp chmod ln grep touch sync df stat readlink rmdir rm; do
  command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if [[ -n "$IMAGE" && "$IMAGE" == *.xz ]]; then
  command -v xz >/dev/null 2>&1 || missing+=("xz")
fi
command -v e2fsck >/dev/null 2>&1 || missing+=("e2fsck (package e2fsprogs)")
command -v resize2fs >/dev/null 2>&1 || missing+=("resize2fs (package e2fsprogs)")
command -v parted >/dev/null 2>&1 || missing+=("parted")
if [[ ${#missing[@]} -gt 0 ]]; then
  die "missing required tools: ${missing[*]}
  On Debian or Raspberry Pi OS install them with:
    sudo apt-get install -y util-linux mount file xz-utils e2fsprogs parted dosfstools"
fi

[[ -n "$IMAGE" ]] || die "no source image. Pass --image <raspios-lite-arm64.img[.xz]>.
  Download one from https://www.raspberrypi.com/software/operating-systems/"
[[ -f "$IMAGE" ]] || die "source image not found: $IMAGE"

[[ "$GROW_MB" =~ ^[0-9]+$ ]] || die "--grow-mb must be a non-negative integer; got '$GROW_MB'"
[[ "$WIFI_COUNTRY" =~ ^[A-Za-z]{2}$ ]] || die "--wifi-country must be a two-letter country code"
[[ "$HOSTNAME" =~ ^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$ ]] || \
  die "--hostname must be a single valid DNS label (1-63 letters, digits, or hyphens)"

for required in \
  "$HERE/files/sysusers-solwear.conf" \
  "$HERE/files/tmpfiles-solwear.conf" \
  "$HERE/files/90-solwear-backlight.rules" \
  "$HERE/files/50-solwear-networkmanager.rules" \
  "$HERE/files/solwear-ui-launch.sh" \
  "$HERE/files/solwear-firstboot.sh" \
  "$HERE/systemd/solweard.service" \
  "$HERE/systemd/solwear-ui.service" \
  "$HERE/systemd/solwear-firstboot.service"; do
  [[ -f "$required" ]] || die "image payload file is missing: $required"
done

image_real="$(readlink -f "$IMAGE")"
output_parent="$(dirname "$OUTPUT")"
[[ -d "$output_parent" ]] || die "output directory does not exist: $output_parent"
output_real="$(readlink -f "$OUTPUT")"
[[ "$image_real" != "$output_real" ]] || die "source image and output resolve to the same file"

[[ -f "$SOLWEARD_BIN" ]] || die "solweard binary not found at: $SOLWEARD_BIN
  Build it for the device first:
    rustup target add aarch64-unknown-linux-gnu
    cargo build --release --target aarch64-unknown-linux-gnu --manifest-path os/solweard/Cargo.toml
  or build it natively on a Raspberry Pi and pass --solweard <path>."

if ! file -b "$SOLWEARD_BIN" | grep -q "ARM aarch64"; then
  die "the binary at $SOLWEARD_BIN is not an aarch64 ELF executable.
  file reports: $(file -b "$SOLWEARD_BIN")"
fi

[[ -f "$SHELL_DIST/index.html" ]] || die "no built shell at $SHELL_DIST
  Build it first:
    (cd os/shell && npm install && npm run build)"

if [[ -n "$APPS_DIR" && ! -d "$APPS_DIR" ]]; then
  die "preinstall directory not found: $APPS_DIR"
fi

if [[ -n "$WIFI_SSID" && -z "$WIFI_PSK" ]]; then
  die "--wifi-ssid was given without --wifi-psk. Provide both, or neither for an open first boot."
fi
if [[ "$WIFI_SSID" == *$'\n'* || "$WIFI_SSID" == *$'\r'* || ${#WIFI_SSID} -gt 32 ]]; then
  die "--wifi-ssid must be at most 32 characters and contain no newlines"
fi
if [[ -n "$WIFI_PSK" ]]; then
  if [[ ${#WIFI_PSK} -eq 64 ]]; then
    [[ "$WIFI_PSK" =~ ^[0-9A-Fa-f]{64}$ ]] || die "a 64-character Wi-Fi PSK must be hexadecimal"
  elif [[ ${#WIFI_PSK} -lt 8 || ${#WIFI_PSK} -gt 63 || "$WIFI_PSK" == *$'\n'* || "$WIFI_PSK" == *$'\r'* ]]; then
    die "--wifi-psk must be 8-63 characters (or 64 hexadecimal characters) and contain no newlines"
  fi
fi

if [[ ! "$SCREEN" =~ ^[0-9]+x[0-9]+:(round|square|rect)$ ]]; then
  die "--screen must look like 480x480:round (shape is round, square, or rect); got '$SCREEN'"
fi

if [[ -e "$OUTPUT" && $FORCE -ne 1 ]]; then
  die "output already exists: $OUTPUT (pass --force to overwrite)"
fi

# Space for the copy plus the growth, with a little headroom.
source_bytes=$(stat -c %s "$IMAGE")
if [[ "$IMAGE" == *.xz ]]; then
  source_bytes="$(xz --robot --list "$IMAGE" | awk -F '\t' '$1 == "totals" { print $5 }')"
  [[ "$source_bytes" =~ ^[0-9]+$ ]] || die "could not determine the uncompressed image size"
fi
needed_mb=$(((source_bytes / 1024 / 1024) + GROW_MB + 256))
avail_mb=$(df -Pm "$(dirname "$(readlink -f "$OUTPUT" 2>/dev/null || echo "$OUTPUT")")" | awk 'NR==2 {print $4}')
if [[ -n "$avail_mb" && "$avail_mb" -lt "$needed_mb" ]]; then
  die "not enough free space: about ${needed_mb} MiB is needed, ${avail_mb} MiB is available"
fi

note "environment looks good"

# --- cleanup ---------------------------------------------------------------

LOOP=""
MOUNT_ROOT=""
MOUNT_BOOT=""
SUCCESS=0

cleanup() {
  set +e
  [[ -n "$MOUNT_BOOT" ]] && mountpoint -q "$MOUNT_BOOT" && umount "$MOUNT_BOOT"
  [[ -n "$MOUNT_ROOT" ]] && mountpoint -q "$MOUNT_ROOT" && umount "$MOUNT_ROOT"
  [[ -n "$MOUNT_BOOT" ]] && rmdir "$MOUNT_BOOT" 2>/dev/null
  [[ -n "$MOUNT_ROOT" ]] && rmdir "$MOUNT_ROOT" 2>/dev/null
  [[ -n "$LOOP" ]] && losetup -d "$LOOP" 2>/dev/null
  if [[ $SUCCESS -ne 1 && -f "$OUTPUT" ]]; then
    echo "==> build failed, removing the partial image $OUTPUT" >&2
    rm -f "$OUTPUT"
  fi
}
trap cleanup EXIT

# --- copy and grow ---------------------------------------------------------

note "copying the source image to $OUTPUT"
if [[ "$IMAGE" == *.xz ]]; then
  xz --decompress --stdout "$IMAGE" > "$OUTPUT"
else
  cp --sparse=always "$IMAGE" "$OUTPUT"
fi

if [[ "$GROW_MB" -gt 0 ]]; then
  note "growing the image by ${GROW_MB} MiB for the shell, the daemon and app storage"
  truncate -s "+${GROW_MB}M" "$OUTPUT"
fi

LOOP="$(losetup --show --find --partscan "$OUTPUT")"
[[ -n "$LOOP" ]] || die "could not attach a loop device to $OUTPUT"
note "attached $LOOP"

BOOT_PART="${LOOP}p1"
ROOT_PART="${LOOP}p2"
[[ -b "$BOOT_PART" && -b "$ROOT_PART" ]] || die "expected two partitions in the image; this does not look like a Raspberry Pi OS image"

if [[ "$GROW_MB" -gt 0 ]]; then
  note "expanding the root filesystem into the new space"
  parted -s "$LOOP" resizepart 2 100% >/dev/null
  partprobe "$LOOP" || true
  fsck_status=0
  e2fsck -p -f "$ROOT_PART" >/dev/null || fsck_status=$?
  [[ $fsck_status -le 1 ]] || die "filesystem check failed with status $fsck_status"
  resize2fs "$ROOT_PART" >/dev/null
fi

MOUNT_ROOT="$(mktemp -d /tmp/solwear-root.XXXXXX)"
MOUNT_BOOT="$(mktemp -d /tmp/solwear-boot.XXXXXX)"
mount "$ROOT_PART" "$MOUNT_ROOT"
mount "$BOOT_PART" "$MOUNT_BOOT"
note "mounted the image"

# --- payload ---------------------------------------------------------------

note "installing solweard and the shell"
install -D -m 0755 "$SOLWEARD_BIN" "$MOUNT_ROOT/usr/bin/solweard"
install -d -m 0755 "$MOUNT_ROOT/usr/share/solwear/shell"
cp -a "$SHELL_DIST/." "$MOUNT_ROOT/usr/share/solwear/shell/"
chmod -R a+rX "$MOUNT_ROOT/usr/share/solwear/shell"

install -d -m 0755 "$MOUNT_ROOT/usr/share/solwear/apps"
if [[ -n "$APPS_DIR" ]]; then
  shopt -s nullglob
  packages=("$APPS_DIR"/*.swa)
  shopt -u nullglob
  if [[ ${#packages[@]} -gt 0 ]]; then
    note "staging ${#packages[@]} package(s) for first-boot install"
    cp -a "${packages[@]}" "$MOUNT_ROOT/usr/share/solwear/apps/"
  fi
fi

note "creating the solwear system user"
install -D -m 0644 "$HERE/files/sysusers-solwear.conf" "$MOUNT_ROOT/usr/lib/sysusers.d/solwear.conf"
install -d -m 0755 "$MOUNT_ROOT/var/lib/solwear"
install -d -m 0755 "$MOUNT_ROOT/var/lib/solwear/apps"
install -D -m 0644 "$HERE/files/tmpfiles-solwear.conf" "$MOUNT_ROOT/usr/lib/tmpfiles.d/solwear.conf"

note "installing configuration"
install -d -m 0755 "$MOUNT_ROOT/etc/solwear"
cat > "$MOUNT_ROOT/etc/solwear/solweard.env" <<EOF
# Environment for solweard.service. Edit on the device and restart the unit.
SOLWEAR_HAL=pi
SOLWEAR_SCREEN=$SCREEN
SOLWEAR_DATA_DIR=/var/lib/solwear
SOLWEAR_SHELL_DIR=/usr/share/solwear/shell
SOLWEAR_LOG=info
EOF
chmod 0644 "$MOUNT_ROOT/etc/solwear/solweard.env"

install -D -m 0644 "$HERE/files/90-solwear-backlight.rules" \
  "$MOUNT_ROOT/etc/udev/rules.d/90-solwear-backlight.rules"
install -D -m 0644 "$HERE/files/50-solwear-networkmanager.rules" \
  "$MOUNT_ROOT/etc/polkit-1/rules.d/50-solwear-networkmanager.rules"
install -D -m 0755 "$HERE/files/solwear-ui-launch.sh" "$MOUNT_ROOT/usr/lib/solwear/solwear-ui-launch.sh"
install -D -m 0755 "$HERE/files/solwear-firstboot.sh" "$MOUNT_ROOT/usr/lib/solwear/solwear-firstboot.sh"

note "installing systemd units"
for unit in solweard.service solwear-ui.service solwear-firstboot.service; do
  install -D -m 0644 "$HERE/systemd/$unit" "$MOUNT_ROOT/etc/systemd/system/$unit"
done

# Units are enabled the way `systemctl enable` would, by hand, because the
# image's systemd cannot run on the build host.
install -d -m 0755 "$MOUNT_ROOT/etc/systemd/system/multi-user.target.wants"
for unit in solweard.service solwear-ui.service solwear-firstboot.service; do
  ln -sf "/etc/systemd/system/$unit" \
    "$MOUNT_ROOT/etc/systemd/system/multi-user.target.wants/$unit"
done

# A kiosk has no console to log into, so the graphical getty is left alone but
# the boot is made quiet and the login prompt is not needed on tty1.
if [[ -f "$MOUNT_ROOT/etc/systemd/system/getty@tty1.service.d/autologin.conf" ]]; then
  rm -f "$MOUNT_ROOT/etc/systemd/system/getty@tty1.service.d/autologin.conf"
fi

note "setting the hostname to $HOSTNAME"
echo "$HOSTNAME" > "$MOUNT_ROOT/etc/hostname"
sed -i "s/^127\.0\.1\.1.*/127.0.1.1\t$HOSTNAME/" "$MOUNT_ROOT/etc/hosts" || true

if [[ -n "$WIFI_SSID" ]]; then
  note "provisioning Wi-Fi for '$WIFI_SSID'"
  install -d -m 0700 "$MOUNT_ROOT/etc/NetworkManager/system-connections"
  profile="$MOUNT_ROOT/etc/NetworkManager/system-connections/solwear-wifi.nmconnection"
  cat > "$profile" <<EOF
[connection]
id=solwear-wifi
type=wifi
autoconnect=true
autoconnect-priority=10

[wifi]
mode=infrastructure
ssid=$WIFI_SSID

[wifi-security]
key-mgmt=wpa-psk
psk=$WIFI_PSK

[ipv4]
method=auto

[ipv6]
method=auto
EOF
  chmod 0600 "$profile"
  echo "country=$WIFI_COUNTRY" > "$MOUNT_ROOT/etc/solwear/wifi-country"
  # The radio stays blocked until a regulatory domain is known.
  install -D -m 0644 /dev/stdin "$MOUNT_ROOT/etc/default/crda" <<EOF
REGDOMAIN=$WIFI_COUNTRY
EOF
fi

if [[ $ENABLE_SSH -eq 1 ]]; then
  note "enabling SSH for headless recovery"
  touch "$MOUNT_BOOT/ssh"
fi

# Raspberry Pi OS Bookworm keeps the boot configuration in /boot/firmware once
# running, but the partition itself is what is mounted here.
CONFIG_TXT="$MOUNT_BOOT/config.txt"
if [[ -f "$CONFIG_TXT" ]]; then
  note "configuring the display stack"
  if ! grep -q "^# SolWear" "$CONFIG_TXT"; then
    cat >> "$CONFIG_TXT" <<'EOF'

# SolWear
# The kiosk uses the KMS driver so cage can drive the panel directly.
dtoverlay=vc4-kms-v3d
max_framebuffers=2
disable_splash=1
EOF
  fi
fi

CMDLINE="$MOUNT_BOOT/cmdline.txt"
if [[ -f "$CMDLINE" ]] && ! grep -q "logo.nologo" "$CMDLINE"; then
  note "quietening the boot"
  sed -i '1 s/$/ logo.nologo consoleblank=0 vt.global_cursor_default=0/' "$CMDLINE"
fi

note "applying read-only friendly mount options"
# Full read-only root is not practical while apps install to /var/lib/solwear,
# so the image reduces writes instead: no access times, and volatile logs.
sed -i 's|\(\s\)\(/\s\+ext4\s\+\)defaults|\1\2defaults,noatime|' "$MOUNT_ROOT/etc/fstab" || true
if ! grep -q "/var/log" "$MOUNT_ROOT/etc/fstab"; then
  cat >> "$MOUNT_ROOT/etc/fstab" <<'EOF'
tmpfs /var/log tmpfs defaults,noatime,nosuid,nodev,size=32M 0 0
tmpfs /tmp     tmpfs defaults,noatime,nosuid,nodev,size=64M 0 0
EOF
fi

sync
SUCCESS=1
note "done: $OUTPUT"
cat <<EOF

Flash it with Raspberry Pi Imager, or:
  sudo dd if=$OUTPUT of=/dev/sdX bs=4M conv=fsync status=progress

On first boot the device installs cage and Chromium (an internet connection is
needed for that one boot), creates the solwear user, and starts the kiosk.
EOF
