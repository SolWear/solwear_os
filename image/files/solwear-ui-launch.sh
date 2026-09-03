#!/bin/sh
# Start the kiosk browser under cage.
#
# Raspberry Pi OS has shipped Chromium as both `chromium` and
# `chromium-browser` across releases, so the binary is resolved at run time
# rather than baked into the unit.
set -eu

URL="${SOLWEAR_UI_URL:-http://127.0.0.1:8731}"
PROFILE="${SOLWEAR_UI_PROFILE:-/var/lib/solwear/ui}"

for candidate in chromium chromium-browser /usr/bin/chromium /usr/bin/chromium-browser; do
  if command -v "$candidate" >/dev/null 2>&1; then
    BROWSER="$candidate"
    break
  fi
done

if [ -z "${BROWSER:-}" ]; then
  echo "solwear-ui: Chromium is not installed. solwear-firstboot.service installs it;" >&2
  echo "            otherwise run: sudo apt-get install -y chromium cage" >&2
  exit 1
fi

# Wait for the daemon to answer before painting, so the first frame is the
# shell rather than a connection error.
attempt=0
while [ "$attempt" -lt 60 ]; do
  if curl -sf -o /dev/null "$URL/healthz"; then
    break
  fi
  attempt=$((attempt + 1))
  sleep 0.5
done

mkdir -p "$PROFILE"

exec cage -d -- "$BROWSER" \
  --kiosk \
  --incognito \
  --noerrdialogs \
  --no-first-run \
  --no-default-browser-check \
  --disable-infobars \
  --disable-session-crashed-bubble \
  --disable-features=Translate,TranslateUI \
  --check-for-update-interval=31536000 \
  --ozone-platform=wayland \
  --enable-features=UseOzonePlatform \
  --hide-scrollbars \
  --overscroll-history-navigation=0 \
  --disable-pinch \
  --autoplay-policy=no-user-gesture-required \
  --user-data-dir="$PROFILE" \
  "$URL"
