#!/usr/bin/env bash
# Builds libsmalt.so for Kobo firmware 4.x (Qt 5.2) and packages out/KoboRoot.tgz
# (the library alone) and out/Smalt-qt5.zip (.kobo/KoboRoot.tgz plus .adds/bookorbit/).
#
#   ./build-kobo.sh            build in the pinned kobuild Qt5 image (Docker or Podman)
#   ./build-kobo.sh --clean    remove build/kobo-qt5 first
#
# Modelled on Kobalt's platforms/kobo/build.sh (MIT licence, see
# toolchains/LICENSE.kobalt): the cross compiler, sysroot and Qt come from the
# prepared kobuild image pinned in toolchains/sdk-image.sh, and nothing here
# builds Qt. Inside that image (for example in CI), the script runs CMake
# directly.
#
# Environment:
#   CONTAINER_ENGINE   docker (default) or podman
#   JOBS               parallel build jobs (default: number of CPUs)
#   SMALT_KOBO_IMAGE   override the pinned image (testing a new kobuild only)
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir=build/kobo-qt5
toolchain=/tc/qt5/toolchain.cmake

clean=false
for arg in "$@"; do
    case "$arg" in
        --clean) clean=true ;;
        -h|--help) sed -n '2,18p' "$0"; exit 0 ;;
        *) echo "Unknown option: $arg (try --help)" >&2; exit 2 ;;
    esac
done

jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "JOBS must be a positive number" >&2; exit 2; }

# The version is computed on the host, where git sees the checkout.
version=$(sed -n 's/^project(Smalt VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
if describe=$(git -C "$root" describe --always --dirty --abbrev=8 2>/dev/null); then
    version="$version+$describe"
fi

if $clean; then
    rm -rf "${root:?}/$build_dir"
fi
mkdir -p "$root/out"

build_inside() {
    cd "$1"
    cmake -S . -B "$build_dir" -G Ninja \
        -DSMALT_TARGET=kobo \
        -DCMAKE_TOOLCHAIN_FILE=toolchains/kobo-qt5.cmake \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DSMALT_VERSION_OVERRIDE="$2"
    cmake --build "$build_dir" --target koboroot --parallel "$3"
}

if [[ -f "$toolchain" ]]; then
    echo "Building inside the Kobo SDK ($toolchain)"
    build_inside "$root" "$version" "$jobs"
else
    engine=${CONTAINER_ENGINE:-docker}
    image=${SMALT_KOBO_IMAGE:-$(sh "$root/toolchains/sdk-image.sh" qt5)}
    if ! command -v "$engine" >/dev/null 2>&1; then
        echo "The Kobo build needs $engine (or run it inside the kobuild Qt5 image)." >&2
        exit 1
    fi
    if ! "$engine" info >/dev/null 2>&1; then
        echo "Cannot reach the $engine daemon. Start it (for example: sudo dockerd &) and retry." >&2
        exit 1
    fi
    if ! "$engine" image inspect "$image" >/dev/null 2>&1; then
        cached="$root/toolchains/cache/kobuild-qt5.tar"
        if [[ -f "$cached" ]]; then
            echo "Loading the Kobo SDK image from $cached"
            "$engine" load -i "$cached"
        else
            echo "Pulling the Kobo SDK image $image"
            if ! "$engine" pull "$image"; then
                echo >&2
                echo "Could not download the Kobo SDK image." >&2
                echo "It comes from ghcr.io, with layers served by pkg-containers.githubusercontent.com;" >&2
                echo "both hosts must be reachable. Or put a 'docker save' of it at $cached." >&2
                exit 1
            fi
        fi
    fi
    echo "Building in $image"
    "$engine" run --rm --pull=never \
        --user "$(id -u):$(id -g)" -e HOME=/tmp \
        --volume "$root:/work" --workdir /work \
        "$image" bash -euc "$(declare -f build_inside); build_dir='$build_dir'; build_inside /work \"\$1\" \"\$2\"" \
        smalt-build "$version" "$jobs"
fi

echo "Done: $root/out/KoboRoot.tgz and $root/out/Smalt-qt5.zip"
echo "Install: copy KoboRoot.tgz to .kobo/ on the Kobo (or unzip Smalt-qt5.zip onto the drive) and eject."
