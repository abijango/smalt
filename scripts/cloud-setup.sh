#!/usr/bin/env bash
# Setup script for the Claude Code cloud environment.
# Paste this into the environment's setup script field. It installs what the
# desktop build and tests need, then fetches the Kobo cross toolchain so
# ./build-kobo.sh works in cloud sessions.
set -euo pipefail

sudo apt-get update -y
sudo apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config git curl ca-certificates \
  clang-format python3 python3-pip \
  qtbase5-dev qtbase5-dev-tools libqt5sql5-sqlite libsqlite3-dev

# Mock BookOrbit server dependencies (task 11 may add more).
python3 -m pip install --break-system-packages --quiet fastapi uvicorn || true

# Kobo cross toolchain (task 00). Like Kobalt, Smalt builds inside the prepared
# kobuild Qt5 image (cross compiler, sysroot, Qt 5.2), pinned by digest in
# toolchains/sdk-image.sh. Docker's image store is the cache: once pulled, the
# image is reused by every ./build-kobo.sh in this session.
# Needs network access to ghcr.io and pkg-containers.githubusercontent.com.
# Failure here doesn't fail the setup: the desktop build doesn't need it.
fetch_kobo_toolchain() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "docker not installed: skipping the Kobo toolchain"
    return 0
  fi
  if ! docker info >/dev/null 2>&1; then
    echo "starting dockerd"
    (sudo dockerd >/tmp/dockerd.log 2>&1 &)
    for _ in $(seq 1 30); do
      docker info >/dev/null 2>&1 && break
      sleep 1
    done
  fi
  if ! docker info >/dev/null 2>&1; then
    echo "dockerd did not start (see /tmp/dockerd.log): skipping the Kobo toolchain"
    return 0
  fi
  local here image
  here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." 2>/dev/null && pwd || true)
  if [[ -n "$here" && -f "$here/toolchains/sdk-image.sh" ]]; then
    image=$(sh "$here/toolchains/sdk-image.sh" qt5)
  else
    # Setup scripts may run before the checkout; keep in sync with toolchains/sdk-image.sh.
    image=ghcr.io/nicoverbruggen/kobuild@sha256:cfb68715d110d02e91df589f1e8940e9781a91d88b2aaee536f5f3830876fb25
  fi
  if docker image inspect "$image" >/dev/null 2>&1; then
    echo "Kobo toolchain image already present"
  elif docker pull --quiet "$image"; then
    echo "Kobo toolchain image cached: $image"
  else
    echo "could not pull $image: ./build-kobo.sh will retry the download"
  fi
}
fetch_kobo_toolchain || true

echo "smalt cloud setup done"
