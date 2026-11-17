#!/bin/sh
# The prepared Kobo SDK image Smalt builds in, pinned by digest.
#
# Adapted from Kobalt's platforms/sdk-image.sh
# (https://github.com/nicoverbruggen/kobalt, commit 679175046d8c, MIT licence,
# see toolchains/LICENSE.kobalt). Recipes and upstream source pins:
# https://github.com/nicoverbruggen/kobuild
# Qt5 recipe commit: 05283b966cb99098f5b197c96a3847790227db53
#
# The qt5 image holds the arm cross compiler, Qt 5.2 (firmware 4.x), GCC 4.9's
# C++ library and the sysroot, with its CMake toolchain at
# /tc/qt5/toolchain.cmake. Use the same digest Kobalt pins, so Smalt and
# Kobalt build against identical SDKs. Qt 6 firmware is out of scope for now.
set -eu
case "${1:-qt5}" in
    qt5) echo ghcr.io/nicoverbruggen/kobuild@sha256:cfb68715d110d02e91df589f1e8940e9781a91d88b2aaee536f5f3830876fb25 ;;
    *) echo 'Expected qt5' >&2; exit 2 ;;
esac
