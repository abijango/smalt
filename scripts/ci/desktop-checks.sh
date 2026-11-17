#!/usr/bin/env bash
# Desktop checks for CI and for local runs, with a Markdown report.
#
#   scripts/ci/desktop-checks.sh [--install-deps]
#
# Runs, in order and without stopping at the first failure: the format check,
# the CMake configure, the build and ctest. Writes:
#   reports/desktop.md         the report section for the PR comment
#   reports/ctest-junit.xml    ctest's JUnit results
#   reports/desktop-*.log      full logs of each step
# Exits non-zero if any step failed.
#
# CI runs it inside the ubuntu:24.04 container (see .github/workflows/ci.yml);
# scripts/ci/run-local.sh runs it in the same container on your machine.
# --install-deps installs the build tools first (for a bare container).
set -uo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
build_dir=${SMALT_CI_BUILD_DIR:-build/ci-desktop}
reports=reports
mkdir -p "$reports"
rm -f "$reports"/desktop.md "$reports"/desktop-*.log "$reports"/ctest-junit.xml

if [[ "${1:-}" == --install-deps ]]; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -y >"$reports/desktop-deps.log" 2>&1 \
        && apt-get install -y --no-install-recommends \
            build-essential cmake ninja-build pkg-config git ca-certificates clang-format python3 \
            qtbase5-dev qtbase5-dev-tools libqt5sql5-sqlite libsqlite3-dev >>"$reports/desktop-deps.log" 2>&1 \
        || { echo "Installing dependencies failed; see $reports/desktop-deps.log" >&2; exit 1; }
    # The checkout may belong to another user when mounted into the container.
    git config --global --add safe.directory "$root" 2>/dev/null || true
fi

failed=0
step_rows=()

# run_step <id> <title> <command...>: runs a step, records its result and log.
run_step() {
    local id=$1 title=$2
    shift 2
    local log="$reports/desktop-$id.log" start=$SECONDS status
    "$@" >"$log" 2>&1
    status=$?
    local took=$((SECONDS - start))
    if [[ $status -eq 0 ]]; then
        step_rows+=("| $title | ✅ passed | ${took} s |")
    else
        failed=1
        step_rows+=("| $title | ❌ failed (exit $status) | ${took} s |")
    fi
    return $status
}

run_step format "Format (clang-format)" ./scripts/check-format.sh
configured=0
if run_step configure "Configure (CMake, desktop target)" \
        cmake -S . -B "$build_dir" -G Ninja -DSMALT_TARGET=desktop; then
    configured=1
fi
built=0
if [[ $configured -eq 1 ]] && run_step build "Build" cmake --build "$build_dir"; then
    built=1
fi
if [[ $built -eq 1 ]]; then
    run_step tests "Tests (ctest)" ctest --test-dir "$build_dir" --output-on-failure \
        --output-junit "$root/$reports/ctest-junit.xml"
fi

tool_versions() {
    local qt cm cf
    qt=$(qmake -query QT_VERSION 2>/dev/null || echo "?")
    cm=$(cmake --version 2>/dev/null | head -1 | awk '{print $3}')
    cf=$(clang-format --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
    local os
    os=$(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME" || uname -s)
    echo "$os · Qt $qt · CMake $cm · GCC $(gcc -dumpversion 2>/dev/null) · clang-format $cf"
}

# Shows the end of a failed step's log in the report.
log_details() {
    local id=$1 title=$2 log="$reports/desktop-$1.log"
    [[ -s "$log" ]] || return 0
    echo
    echo "<details><summary>❌ $title: last lines of the log</summary>"
    echo
    echo '```'
    tail -n 60 "$log" | cut -c1-300
    echo '```'
    echo "</details>"
}

{
    if [[ $failed -eq 0 ]]; then
        echo "### ✅ Desktop build and tests"
    else
        echo "### ❌ Desktop build and tests"
    fi
    echo
    echo "$(tool_versions)"
    echo
    echo "| Step | Result | Time |"
    echo "| --- | --- | --- |"
    printf '%s\n' "${step_rows[@]}"
    for row in "${step_rows[@]}"; do
        case "$row" in
            *"Format"*"failed"*) log_details format "Format" ;;
            *"Configure"*"failed"*) log_details configure "Configure" ;;
            *"Build"*"failed"*) log_details build "Build" ;;
        esac
    done
    if [[ -f "$reports/ctest-junit.xml" ]]; then
        echo
        echo "#### Tests"
        echo
        python3 "$root/scripts/ci/junit-report.py" "$reports/ctest-junit.xml"
    elif [[ $built -eq 1 ]]; then
        log_details tests "Tests"
    fi
} >"$reports/desktop.md"

# Hand files back to the host user when run as root in a container.
if [[ -n "${HOST_UID:-}" && $(id -u) -eq 0 ]]; then
    chown -R "$HOST_UID:${HOST_GID:-$HOST_UID}" "$reports" "$build_dir" 2>/dev/null || true
fi

cat "$reports/desktop.md"
exit $failed
