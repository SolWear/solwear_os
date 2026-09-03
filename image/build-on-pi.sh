#!/usr/bin/env bash
#
# Build a SolWear OS image on a Raspberry Pi running Raspberry Pi OS.
#
# build-image.sh needs a Linux host with loop devices, which a Mac does not
# have, so the Pi builds its own image. This script is the whole path from a
# fresh checkout to a flashable .img: it installs the toolchain, builds the
# daemon, the shell and the apps, fetches a stock Raspberry Pi OS Lite image,
# and hands all of it to build-image.sh.
#
# Everything it installs comes from apt or rustup. It never writes outside the
# repository, the build directory it is given, and the usual toolchain paths in
# your home directory.
#
# Usage, from the repository root:
#
#   ./image/build-on-pi.sh --wifi-ssid HomeNet --wifi-psk secret --wifi-country UA
#
# Add --screen WxH:shape if the panel is not the 480x480 round default, for
# example --screen 800x480:rect.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/.." && pwd)"

BUILD_DIR="${ROOT}/image/build"
OUTPUT="${BUILD_DIR}/solwear-os.img"
SCREEN="480x480:round"
HOSTNAME="solwear"
WIFI_SSID=""
WIFI_PSK=""
WIFI_COUNTRY="GB"
SKIP_DEPS=0

# The current Raspberry Pi OS Lite arm64 release. `latest` always redirects to
# whatever is current, so this does not go stale.
RASPIOS_URL="https://downloads.raspberrypi.com/raspios_lite_arm64_latest"

note() { printf '\n==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --wifi-ssid) WIFI_SSID="${2:-}"; shift 2 ;;
    --wifi-psk) WIFI_PSK="${2:-}"; shift 2 ;;
    --wifi-country) WIFI_COUNTRY="${2:-}"; shift 2 ;;
    --screen) SCREEN="${2:-}"; shift 2 ;;
    --hostname) HOSTNAME="${2:-}"; shift 2 ;;
    --output) OUTPUT="${2:-}"; shift 2 ;;
    --build-dir) BUILD_DIR="${2:-}"; shift 2 ;;
    --skip-deps) SKIP_DEPS=1; shift ;;
    -h|--help) sed -n '2,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

# --- preflight -------------------------------------------------------------

[[ "$(uname -s)" == "Linux" ]] || die "this script builds the image on Linux. Run it on the Pi, not on a Mac."
[[ "$(uname -m)" == "aarch64" ]] || die "expected an arm64 Raspberry Pi OS. Found $(uname -m)."
[[ -f "${ROOT}/os/solweard/Cargo.toml" ]] || die "run this from the SolWear repository."

free_mb="$(df -Pm "${ROOT}" | awk 'NR == 2 { print $4 }')"
[[ "${free_mb}" -ge 8000 ]] || die "need about 8 GB free, found ${free_mb} MB. The stock image alone unpacks to roughly 3 GB."

mkdir -p "${BUILD_DIR}"

# --- toolchain -------------------------------------------------------------

if [[ "${SKIP_DEPS}" -eq 0 ]]; then
  note "Installing build dependencies"
  sudo apt-get update
  # The first group builds the daemon, the second edits the image.
  sudo apt-get install -y \
    build-essential pkg-config curl ca-certificates git \
    util-linux mount file xz-utils e2fsprogs parted dosfstools

  if ! command -v cargo >/dev/null 2>&1; then
    note "Installing Rust"
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path
  fi

  # Raspberry Pi OS ships Node 18 or 20; the SDK and the emulator need 22.
  node_major="$(node --version 2>/dev/null | sed 's/^v\([0-9]*\).*/\1/')"
  if [[ -z "${node_major}" || "${node_major}" -lt 22 ]]; then
    note "Installing Node 22"
    curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
    sudo apt-get install -y nodejs
  fi
fi

export PATH="${HOME}/.cargo/bin:${PATH}"
command -v cargo >/dev/null 2>&1 || die "cargo is not on PATH. Open a new shell, or source ~/.cargo/env, and retry."

note "Toolchain"
echo "  node   $(node --version)"
echo "  npm    $(npm --version)"
echo "  cargo  $(cargo --version)"

# --- build -----------------------------------------------------------------

note "Building the daemon (this takes a few minutes on a Pi 4)"
( cd "${ROOT}/os/solweard" && cargo build --release )
SOLWEARD="${ROOT}/os/solweard/target/release/solweard"
[[ -x "${SOLWEARD}" ]] || die "the daemon did not build."

note "Building the shell"
( cd "${ROOT}/os/shell" && npm ci --no-audit --no-fund && npm run build )

note "Building the SDK and the CLI"
( cd "${ROOT}/sdk/runtime" && npm ci --no-audit --no-fund && npm run build )
( cd "${ROOT}/sdk/cli" && npm ci --no-audit --no-fund && npm run build )

note "Packaging the preinstalled apps"
APPS_STAGE="${BUILD_DIR}/apps"
rm -rf "${APPS_STAGE}"
mkdir -p "${APPS_STAGE}"
for app in watchface signer store; do
  ( cd "${ROOT}/apps/${app}" && npm ci --no-audit --no-fund && npm run build )
  # Sideloaded first-party apps ship unsigned; the store path still requires a
  # signature, and the daemon still refuses anything whose manifest is invalid.
  ( cd "${ROOT}/apps/${app}" && node "${ROOT}/sdk/cli/dist/bin.js" package )
  cp "${ROOT}/apps/${app}/dist/"*.swa "${APPS_STAGE}/"
done
ls -la "${APPS_STAGE}"

# --- stock image -----------------------------------------------------------

STOCK="${BUILD_DIR}/raspios-lite-arm64.img.xz"
if [[ ! -f "${STOCK}" ]]; then
  note "Downloading Raspberry Pi OS Lite (arm64)"
  curl -fL --progress-bar -o "${STOCK}.part" "${RASPIOS_URL}"
  mv "${STOCK}.part" "${STOCK}"
else
  note "Using the Raspberry Pi OS image already in ${BUILD_DIR}"
fi

# --- image -----------------------------------------------------------------

note "Building the SolWear image"
wifi_args=()
if [[ -n "${WIFI_SSID}" ]]; then
  wifi_args+=(--wifi-ssid "${WIFI_SSID}" --wifi-psk "${WIFI_PSK}" --wifi-country "${WIFI_COUNTRY}")
fi

sudo "${HERE}/build-image.sh" \
  --image "${STOCK}" \
  --output "${OUTPUT}" \
  --solweard "${SOLWEARD}" \
  --shell "${ROOT}/os/shell/dist" \
  --apps "${APPS_STAGE}" \
  --hostname "${HOSTNAME}" \
  --screen "${SCREEN}" \
  --force \
  "${wifi_args[@]}"

note "Done"
ls -lh "${OUTPUT}"
cat <<EOF

Flash it with Raspberry Pi Imager, or from this Pi with:

  sudo dd if=${OUTPUT} of=/dev/sdX bs=4M status=progress conv=fsync

Boot it, and the watch comes up on the panel by itself. To watch it start:

  journalctl -u solweard -u solwear-ui -f
EOF
