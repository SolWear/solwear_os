#!/usr/bin/env bash
#
# Install SolWear OS onto a Raspberry Pi that is already running Raspberry Pi
# OS, without building or flashing an image.
#
# This is the fast path for hardware work: it puts the daemon, the shell and the
# apps on a board you can already reach over SSH, so a change can be tried on
# real hardware in about a minute. Building a flashable image is the other path,
# and that one is `build-on-pi.sh`.
#
# Run it ON the Pi, from a checkout of this repository:
#
#   ./image/install-on-pi.sh --screen 800x480:rect
#
# It expects `os/shell/dist` and the app `.swa` packages to exist already. Build
# them on a development machine and copy them over (rsync of the whole checkout
# works), or pass --build to build them here, which needs Node 22 on the Pi.
#
# What it leaves behind:
#   /usr/bin/solweard              the daemon
#   /usr/share/solwear/shell/      the shell bundle
#   /etc/solwear/solweard.env      daemon configuration
#   /var/lib/solwear/              wallet, installed apps, runtime state
#   solweard.service               enabled and started
#
# It deliberately does NOT touch the desktop or the boot target. The kiosk is
# started separately, so a Pi you are working on keeps its desktop.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/.." && pwd)"

SCREEN="480x480:round"
HAL="pi"
BUILD=0
PACKAGES=""

note() { printf '\n==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --screen) SCREEN="${2:-}"; shift 2 ;;
    --hal) HAL="${2:-}"; shift 2 ;;
    --packages) PACKAGES="${2:-}"; shift 2 ;;
    --build) BUILD=1; shift ;;
    -h|--help) sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done

[[ "$(uname -s)" == "Linux" ]] || die "run this on the Pi, not on a development machine."
[[ -f "${ROOT}/os/solweard/Cargo.toml" ]] || die "run this from a SolWear checkout."

note "Installing build and kiosk dependencies"
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
  build-essential pkg-config curl cage chromium >/dev/null

if ! command -v cargo >/dev/null 2>&1; then
  note "Installing Rust"
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --no-modify-path --profile minimal
fi
export PATH="${HOME}/.cargo/bin:${PATH}"

note "Building the daemon (about five minutes on a Pi 4)"
( cd "${ROOT}/os/solweard" && cargo build --release )
DAEMON="${ROOT}/os/solweard/target/release/solweard"
[[ -x "${DAEMON}" ]] || die "the daemon did not build."

if [[ "${BUILD}" -eq 1 ]]; then
  note "Building the shell, the SDK and the apps"
  ( cd "${ROOT}/sdk/runtime" && npm ci --no-audit --no-fund && npm run build )
  ( cd "${ROOT}/sdk/cli" && npm ci --no-audit --no-fund && npm run build )
  ( cd "${ROOT}/os/shell" && npm ci --no-audit --no-fund && npm run build )
  for app in watchface signer store stats games; do
    ( cd "${ROOT}/apps/${app}" && npm ci --no-audit --no-fund && npm run build && npm run package )
  done
fi

[[ -f "${ROOT}/os/shell/dist/index.html" ]] || die "os/shell/dist is missing. Build it, or pass --build."

note "Installing files"
sudo install -m 0755 "${DAEMON}" /usr/bin/solweard
sudo mkdir -p /usr/share/solwear/shell /etc/solwear /var/lib/solwear/apps
sudo cp -a "${ROOT}/os/shell/dist/." /usr/share/solwear/shell/
sudo cp "${HERE}/systemd/solweard.service" /etc/systemd/system/solweard.service

id solwear >/dev/null 2>&1 || \
  sudo useradd --system --home /var/lib/solwear --shell /usr/sbin/nologin solwear
sudo chown -R solwear:solwear /var/lib/solwear

sudo tee /etc/solwear/solweard.env >/dev/null <<ENV
SOLWEAR_HAL=${HAL}
SOLWEAR_SCREEN=${SCREEN}
SOLWEAR_DATA_DIR=/var/lib/solwear
SOLWEAR_SHELL_DIR=/usr/share/solwear/shell
SOLWEAR_LOG=info
ENV

note "Starting the daemon"
sudo systemctl daemon-reload
sudo systemctl enable --now solweard.service
sleep 2
systemctl is-active --quiet solweard.service || {
  sudo journalctl -u solweard -n 30 --no-pager
  die "the daemon did not start."
}

note "Installing apps"
if [[ -z "${PACKAGES}" ]]; then
  PACKAGES="${ROOT}/apps"
fi
found=0
while IFS= read -r package; do
  sudo -u solwear SOLWEAR_DATA_DIR=/var/lib/solwear /usr/bin/solweard install "${package}" --allow-unsigned
  found=$((found + 1))
done < <(find "${PACKAGES}" -name '*.swa' -print | sort)
[[ "${found}" -gt 0 ]] || echo "no .swa packages found under ${PACKAGES}; the watch will start with none"
sudo systemctl restart solweard.service

note "Done"
cat <<EOF
The daemon serves the shell on http://127.0.0.1:8731/ and JSON-RPC on
ws://127.0.0.1:8730/. Both are bound to loopback.

To see the watch on this Pi's display, from a session on the Pi:

  chromium --kiosk --app=http://127.0.0.1:8731/ --password-store=basic

To make it start at boot instead of the desktop, which replaces the desktop
session with the watch:

  sudo systemctl disable lightdm
  sudo cp ${HERE}/systemd/solwear-ui.service /etc/systemd/system/
  sudo systemctl enable --now solwear-ui

To follow what the daemon is doing:

  journalctl -u solweard -f
EOF
