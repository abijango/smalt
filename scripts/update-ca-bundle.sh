#!/usr/bin/env bash
# Refreshes packaging/adds/bookorbit/certs/ca-bundle.pem, the CA certificates
# Smalt ships because the Kobo's own may be too old for current Let's Encrypt
# chains (docs/SPEC.md, Sync engine, "Networking").
#
# Source: curl's cacert.pem (https://curl.se/docs/caextract.html), extracted
# from Mozilla's root store. Its data is under the Mozilla Public License 2.0;
# the file's own header keeps the licence notice and the extraction date.
# The download is checked against curl's published SHA-256 before it replaces
# the bundle.
#
# Run before each release, or about every three months:
#   scripts/update-ca-bundle.sh
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
target="$root/packaging/adds/bookorbit/certs/ca-bundle.pem"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

curl -fsSL --proto '=https' https://curl.se/ca/cacert.pem -o "$work/cacert.pem"
curl -fsSL --proto '=https' https://curl.se/ca/cacert.pem.sha256 -o "$work/cacert.pem.sha256"
expected=$(awk '{print $1}' "$work/cacert.pem.sha256")
actual=$(sha256sum "$work/cacert.pem" | awk '{print $1}')
if [[ -z "$expected" || "$expected" != "$actual" ]]; then
    echo "Checksum mismatch: expected '$expected', got '$actual'" >&2
    exit 1
fi
count=$(grep -c 'BEGIN CERTIFICATE' "$work/cacert.pem")
if [[ "$count" -lt 100 ]]; then
    echo "Only $count certificates in the download; refusing to replace the bundle" >&2
    exit 1
fi
install -m 0644 "$work/cacert.pem" "$target"
echo "Updated $target: $count certificates, $(grep -m1 -i 'as of' "$target" | sed 's/^## *//')"
