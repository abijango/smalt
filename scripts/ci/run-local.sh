#!/usr/bin/env bash
# Runs the same checks as CI, in the same Docker containers, on your machine.
#
#   scripts/ci/run-local.sh            desktop and Kobo checks
#   scripts/ci/run-local.sh desktop    desktop checks only
#   scripts/ci/run-local.sh kobo       Kobo package checks only
#
# Needs Docker (or Podman via CONTAINER_ENGINE). Writes reports/*.md and the
# combined reports/pr-comment.md, the same text CI posts on the PR.
set -uo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
engine=${CONTAINER_ENGINE:-docker}
what=${1:-all}
status=0

# Behind a TLS-intercepting proxy, the host's CA bundle (PIP_CERT or
# SSL_CERT_FILE) is passed into the container so pip can download.
ca_args=()
ca=${PIP_CERT:-${SSL_CERT_FILE:-}}
if [[ -n "$ca" && -f "$ca" ]]; then
    ca_args=(-v "$ca:/etc/smalt-host-ca.pem:ro" -e PIP_CERT=/etc/smalt-host-ca.pem)
fi

if [[ $what == all || $what == desktop ]]; then
    "$engine" run --rm "${ca_args[@]}" \
        -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
        -v "$root:/work" -w /work \
        "$(sh scripts/ci/desktop-image.sh)" \
        bash scripts/ci/desktop-checks.sh --install-deps || status=1
fi
if [[ $what == all || $what == kobo ]]; then
    scripts/ci/kobo-checks.sh || status=1
fi
scripts/ci/combine-reports.sh reports
exit $status
