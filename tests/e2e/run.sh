#!/usr/bin/env bash
#
# Build everything the end-to-end test needs, then run it.
#
# The test drives real components, so they have to exist: the compiled daemon,
# the built CLI, the built SDK the CLI links apps against, and the built shell
# the daemon serves. Pass --no-build to skip straight to the test when they are
# already up to date.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

build=1
for argument in "$@"; do
  case "$argument" in
    --no-build) build=0 ;;
    -h | --help)
      echo "Usage: tests/e2e/run.sh [--no-build]"
      exit 0
      ;;
    *)
      echo "unknown option: $argument" >&2
      exit 2
      ;;
  esac
done

node_major="$(node -p 'process.versions.node.split(".")[0]')"
if [ "$node_major" -lt 22 ]; then
  echo "This test needs Node 22 or newer: it uses the global WebSocket client." >&2
  echo "Found Node $(node -v)." >&2
  exit 1
fi

if [ "$build" -eq 1 ]; then
  echo "==> building the daemon"
  cargo build --manifest-path "$root/os/solweard/Cargo.toml"

  for component in sdk/runtime sdk/cli os/shell; do
    echo "==> building $component"
    if [ ! -d "$root/$component/node_modules" ]; then
      npm --prefix "$root/$component" install
    fi
    npm --prefix "$root/$component" run build
  done
fi

echo "==> running the end-to-end test"
cd "$root"
exec node --test tests/e2e/e2e.test.mjs
