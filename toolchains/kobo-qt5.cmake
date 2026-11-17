# CMake toolchain for the Kobo Qt5 target (firmware 4.x, Qt 5.2).
#
# Adapted from Kobalt's platforms/kobo/toolchains/qt5.cmake
# (https://github.com/nicoverbruggen/kobalt, commit 679175046d8c,
# MIT licence, see toolchains/LICENSE.kobalt). Like Kobalt, Smalt never builds
# Qt or the cross compiler: it uses the toolchain inside the prepared kobuild
# Qt5 image pinned in toolchains/sdk-image.sh. build-kobo.sh runs the build in
# that image.
set(SMALT_KOBO_QT5_TOOLCHAIN "/tc/qt5/toolchain.cmake" CACHE FILEPATH "Prepared Kobo Qt5 toolchain")
if(NOT EXISTS "${SMALT_KOBO_QT5_TOOLCHAIN}")
    message(FATAL_ERROR "Prepared Kobo Qt5 toolchain missing at ${SMALT_KOBO_QT5_TOOLCHAIN}. "
        "Run ./build-kobo.sh, which builds inside the pinned kobuild Qt5 image; never build Qt.")
endif()
include("${SMALT_KOBO_QT5_TOOLCHAIN}")
