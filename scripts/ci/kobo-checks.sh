#!/usr/bin/env bash
# Kobo package checks for CI and for local runs, with a Markdown report.
#
#   scripts/ci/kobo-checks.sh [--skip-build]
#
# Builds out/KoboRoot.tgz with ./build-kobo.sh (inside the pinned kobuild Qt5
# image), then checks the library that actually ships in the package:
#   - 32-bit ARM, hard-float (Kobo Clara and Libra)
#   - needs no newer glibc than firmware 4's 2.19, and no newer C++ runtime
#     than its GCC 4.9 (GLIBCXX 3.4.20, CXXABI 1.3.8)
#   - exports NickelHook's and Qt's plugin entry points
#   - package layout (packaging/check-koboroot.cmake)
# Writes reports/kobo.md and reports/kobo-build.log. Exits non-zero on failure.
set -uo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
reports=reports
mkdir -p "$reports"
rm -f "$reports/kobo.md" "$reports/kobo-build.log"

max_glibc=2.19
max_glibcxx=3.4.20
max_cxxabi=1.3.8

failed=0
rows=()
pass() { rows+=("| $1 | ✅ $2 |"); }
fail() { rows+=("| $1 | ❌ $2 |"); failed=1; }

# version_le A B: true when A <= B.
version_le() { [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" == "$1" ]]; }

start=$SECONDS
built=1
if [[ "${1:-}" != --skip-build ]]; then
    if ./build-kobo.sh >"$reports/kobo-build.log" 2>&1; then
        pass "Build (\`./build-kobo.sh\`, kobuild Qt5 image)" "built in $((SECONDS - start)) s"
    else
        fail "Build (\`./build-kobo.sh\`, kobuild Qt5 image)" "failed after $((SECONDS - start)) s"
        built=0
    fi
fi

package=out/KoboRoot.tgz
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
lib="$work/usr/local/Kobo/imageformats/libsmalt.so"
listing=""

if [[ $built -eq 1 && -f "$package" ]] && tar -xzf "$package" -C "$work" 2>/dev/null; then
    listing=$(tar -tzvf "$package")
    if cmake "-DARCHIVE=$package" -P packaging/check-koboroot.cmake >"$work/layout.log" 2>&1; then
        pass "Package layout" "library and \`.adds/bookorbit/\` files present, owned by root, no \`config.ini\`"
    else
        fail "Package layout" "$(grep -m1 -i error -A2 "$work/layout.log" | tr '\n' ' ' | cut -c1-200)"
    fi

    if [[ -f "$lib" ]]; then
        header=$(readelf -h "$lib")
        if grep -q "ELF32" <<<"$header" && grep -q "Machine:.*ARM" <<<"$header" \
                && grep -q "hard-float" <<<"$header"; then
            pass "Architecture" "32-bit ARM, EABI5, hard-float"
        else
            fail "Architecture" "not a 32-bit hard-float ARM library"
        fi

        versions=$(readelf -V "$lib")
        newest() { grep -oE "$1_[0-9.]+" <<<"$versions" | sed "s/^$1_//" | sort -uV | tail -1; }
        for pair in "GLIBC:$max_glibc:glibc" "GLIBCXX:$max_glibcxx:C++ library (GCC 4.9)" \
                "CXXABI:$max_cxxabi:C++ ABI (GCC 4.9)"; do
            IFS=: read -r tag limit label <<<"$pair"
            got=$(newest "$tag")
            if [[ -z "$got" ]]; then
                pass "Needs $label" "no $tag symbols"
            elif version_le "$got" "$limit"; then
                pass "Needs $label" "\`${tag}_$got\` (firmware has up to $limit)"
            else
                fail "Needs $label" "\`${tag}_$got\` is newer than the firmware's $limit"
            fi
        done

        symbols=$(readelf --dyn-syms -W "$lib")
        missing=()
        for sym in NickelHook qt_plugin_instance qt_plugin_query_metadata; do
            grep -qE " $sym$" <<<"$symbols" || missing+=("$sym")
        done
        if [[ ${#missing[@]} -eq 0 ]]; then
            pass "Entry points" "\`NickelHook\`, \`qt_plugin_instance\`, \`qt_plugin_query_metadata\` exported"
        else
            fail "Entry points" "missing: ${missing[*]}"
        fi

        needed=$(readelf -d "$lib" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p' | tr '\n' ' ')
        size=$(stat -c %s "$lib")
    else
        fail "Library" "\`libsmalt.so\` not found in the package"
    fi
elif [[ $built -eq 1 ]]; then
    fail "Package" "\`$package\` missing or unreadable"
fi

{
    if [[ $failed -eq 0 ]]; then
        echo "### ✅ Kobo package (firmware 4.x, Qt 5.2)"
    else
        echo "### ❌ Kobo package (firmware 4.x, Qt 5.2)"
    fi
    echo
    echo "| Check | Result |"
    echo "| --- | --- |"
    printf '%s\n' "${rows[@]}"
    if [[ -n "${needed:-}" ]]; then
        echo
        echo "Library: $(numfmt --to=iec "${size:-0}" 2>/dev/null || echo "${size:-?} bytes") stripped; links \`${needed% }\`."
    fi
    if [[ -n "$listing" ]]; then
        echo
        echo "<details><summary>Package contents</summary>"
        echo
        echo '```'
        awk '{print $1, $3, $6}' <<<"$listing"
        echo '```'
        echo "</details>"
    fi
    if [[ $built -eq 0 && -s "$reports/kobo-build.log" ]]; then
        echo
        echo "<details><summary>❌ Build: last lines of the log</summary>"
        echo
        echo '```'
        tail -n 60 "$reports/kobo-build.log" | cut -c1-300
        echo '```'
        echo "</details>"
    fi
} >"$reports/kobo.md"

cat "$reports/kobo.md"
exit $failed
