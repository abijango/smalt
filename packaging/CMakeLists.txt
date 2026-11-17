# Owned by task 00 (scaffold); task 30 adds the NickelMenu entry.
#
#   resources/       files libsmalt.so embeds and writes to .adds/bookorbit/ on
#                    first load (the `uninstall` marker, config.ini.example)
#   adds/bookorbit/  files shipped in Smalt-qt5.zip next to .kobo/KoboRoot.tgz
#
# Kobo target: `cmake --build <dir> --target koboroot` writes
# <repo>/out/KoboRoot.tgz and <repo>/out/Smalt-qt5.zip (build-kobo.sh calls it).
# Desktop target: tests pack a dummy library with the same script, check the
# package layout, and run the first-load seeding through an install's life.
include("${PROJECT_SOURCE_DIR}/cmake/SmaltEmbed.cmake")
file(GLOB smalt_seeded CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")

if(SMALT_TARGET STREQUAL "kobo")
    set(out "${PROJECT_SOURCE_DIR}/out")
    file(GLOB_RECURSE smalt_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/adds/*")
    add_custom_command(OUTPUT "${out}/KoboRoot.tgz" "${out}/Smalt-qt5.zip"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${out}"
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:smalt>" "-DSTRIP=${CMAKE_STRIP}"
            "-DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/adds" "-DOUTDIR=${out}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/package-koboroot.cmake"
        COMMAND "${CMAKE_COMMAND}" "-DOUTDIR=${out}" -P "${CMAKE_CURRENT_SOURCE_DIR}/check-koboroot.cmake"
        DEPENDS smalt ${smalt_assets} package-koboroot.cmake check-koboroot.cmake
        VERBATIM)
    add_custom_target(koboroot ALL DEPENDS "${out}/KoboRoot.tgz" "${out}/Smalt-qt5.zip")
else()
    # Package layout, with a dummy library.
    set(fake "${CMAKE_CURRENT_BINARY_DIR}/fake")
    file(WRITE "${fake}/libsmalt.so" "not a real library\n")
    add_test(NAME packaging.pack
        COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=${fake}/libsmalt.so" "-DASSETS=${CMAKE_CURRENT_SOURCE_DIR}/adds"
            "-DOUTDIR=${fake}" -P "${CMAKE_CURRENT_SOURCE_DIR}/package-koboroot.cmake")
    add_test(NAME packaging.layout
        COMMAND "${CMAKE_COMMAND}" "-DOUTDIR=${fake}" -P "${CMAKE_CURRENT_SOURCE_DIR}/check-koboroot.cmake")
    set_tests_properties(packaging.pack PROPERTIES FIXTURES_SETUP koboroot)
    set_tests_properties(packaging.layout PROPERTIES FIXTURES_REQUIRED koboroot)

    # First-load seeding (toolchains/nickelhook/resources.c), on the host.
    set(nickelhook "${PROJECT_SOURCE_DIR}/toolchains/nickelhook")
    smalt_embed_resources("${CMAKE_CURRENT_BINARY_DIR}/seed/embedded_resources.h" ${smalt_seeded})
    add_executable(seed_test test/seed_test.c "${nickelhook}/resources.c")
    set_target_properties(seed_test PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    target_include_directories(seed_test PRIVATE "${nickelhook}" "${CMAKE_CURRENT_BINARY_DIR}/seed")
    target_compile_definitions(seed_test PRIVATE
        SMALT_SEED_ROOT="${CMAKE_CURRENT_BINARY_DIR}/seed-root")
    add_test(NAME packaging.first_load_seeding
        COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:seed_test>"
            "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/seed-root" -P "${CMAKE_CURRENT_SOURCE_DIR}/test/seed-test.cmake")
endif()
