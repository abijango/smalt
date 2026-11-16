# Owned by task 00 (scaffold); task 30 adds the NickelMenu entry.
#
# Kobo target: `cmake --build <dir> --target koboroot` writes
# <repo>/out/KoboRoot.tgz (build-kobo.sh calls it).
# Desktop target: a test packs a dummy library with the same script and
# checks the archive layout, so packaging is tested without the toolchain.
set(SMALT_PACKAGING_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

if(SMALT_TARGET STREQUAL "kobo")
    set(koboroot "${PROJECT_SOURCE_DIR}/out/KoboRoot.tgz")
    file(GLOB_RECURSE smalt_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/adds/*")
    add_custom_command(OUTPUT "${koboroot}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_SOURCE_DIR}/out"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:smalt>" "-DSTRIP=${CMAKE_STRIP}"
            "-DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/adds" "-DOUTPUT=${koboroot}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/package-koboroot.cmake"
        COMMAND "${CMAKE_COMMAND}" "-DARCHIVE=${koboroot}" -P "${CMAKE_CURRENT_SOURCE_DIR}/check-koboroot.cmake"
        DEPENDS smalt ${smalt_assets} package-koboroot.cmake check-koboroot.cmake
        VERBATIM)
    add_custom_target(koboroot ALL DEPENDS "${koboroot}")
else()
    set(fake_lib "${CMAKE_CURRENT_BINARY_DIR}/fake/libsmalt.so")
    set(fake_out "${CMAKE_CURRENT_BINARY_DIR}/fake/KoboRoot.tgz")
    file(WRITE "${fake_lib}" "not a real library\n")
    add_test(NAME packaging.koboroot_pack
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=${fake_lib}" "-DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/adds"
            "-DOUTPUT=${fake_out}" -P "${CMAKE_CURRENT_SOURCE_DIR}/package-koboroot.cmake")
    add_test(NAME packaging.koboroot_layout
        COMMAND "${CMAKE_COMMAND}" "-DARCHIVE=${fake_out}" -P "${CMAKE_CURRENT_SOURCE_DIR}/check-koboroot.cmake")
    set_tests_properties(packaging.koboroot_pack PROPERTIES FIXTURES_SETUP koboroot)
    set_tests_properties(packaging.koboroot_layout PROPERTIES FIXTURES_REQUIRED koboroot)
endif()
