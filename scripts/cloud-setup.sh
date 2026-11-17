#!/usr/bin/env bash
# Setup script for the Claude Code cloud environment.
# Paste this into the environment's setup script field. It installs what the
# desktop build and tests need. Task 00 extends it with the Kobo cross toolchain.
set -euo pipefail

sudo apt-get update -y
sudo apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config git curl ca-certificates \
  clang-format python3 python3-pip \
  qtbase5-dev qtbase5-dev-tools libqt5sql5-sqlite libsqlite3-dev

# Mock BookOrbit server dependencies (task 11 may add more).
python3 -m pip install --break-system-packages --quiet fastapi uvicorn || true

# TODO (task 00): fetch and cache the Kobo cross toolchain here, based on
# Kobalt's toolchains/ setup, so ./build-kobo.sh works in cloud sessions.
echo "smalt cloud setup done"
