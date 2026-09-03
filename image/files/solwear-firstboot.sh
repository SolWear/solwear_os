#!/bin/bash
# First boot provisioning.
#
# The image build runs on a host that cannot execute aarch64 packages, so the
# two display-stack packages are installed here instead, along with any
# packages staged into /usr/share/solwear/apps. The unit disables itself once
# it has succeeded.
set -euo pipefail

STAMP=/var/lib/solwear/.firstboot-complete
if [[ -f "$STAMP" ]]; then
  exit 0
fi

echo "solwear-firstboot: checking the display stack"
missing=()
command -v cage >/dev/null 2>&1 || missing+=(cage)
command -v curl >/dev/null 2>&1 || missing+=(curl)
if ! command -v chromium >/dev/null 2>&1 && ! command -v chromium-browser >/dev/null 2>&1; then
  missing+=(chromium)
fi

if [[ ${#missing[@]} -gt 0 ]]; then
  echo "solwear-firstboot: installing ${missing[*]}"
  export DEBIAN_FRONTEND=noninteractive
  if ! apt-get update -qq; then
    echo "solwear-firstboot: apt-get update failed; will retry on the next boot" >&2
    exit 1
  fi
  if ! apt-get install -y --no-install-recommends "${missing[@]}"; then
    echo "solwear-firstboot: package install failed; will retry on the next boot" >&2
    exit 1
  fi
fi

# Set the Wi-Fi regulatory domain, which keeps the radio unblocked.
if [[ -f /etc/solwear/wifi-country ]]; then
  . /etc/solwear/wifi-country
  if [[ -n "${country:-}" ]] && command -v raspi-config >/dev/null 2>&1; then
    raspi-config nonint do_wifi_country "$country" || true
  fi
fi

# Install packages shipped with the image through the same verifier used by
# runtime installs. Unsigned or damaged packages make provisioning retry on
# the next boot instead of being unpacked into the trusted app directory.
install -d -o solwear -g solwear -m 0750 /var/lib/solwear
install -d -o solwear -g solwear -m 0750 /var/lib/solwear/apps
shopt -s nullglob
for package in /usr/share/solwear/apps/*.swa; do
  echo "solwear-firstboot: installing $(basename "$package")"
  runuser --user solwear -- /usr/bin/solweard install "$package"
done
shopt -u nullglob

touch "$STAMP"
systemctl disable solwear-firstboot.service || true
echo "solwear-firstboot: complete"
