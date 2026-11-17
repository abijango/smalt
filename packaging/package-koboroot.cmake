# Builds the Kobo packages. Run with cmake -P:
#
#   cmake -DLIBRARY=<libsmalt.so> -DASSETS=<packaging/adds> -DOUTDIR=<out dir>
#         [-DSTRIP=<arm strip>] -P package-koboroot.cmake
#
# Writes two files, as Kobalt does:
#   <out>/KoboRoot.tgz    only the library; the Kobo extracts it at / on the
#                         next boot: usr/local/Kobo/imageformats/libsmalt.so
#   <out>/Smalt-qt5.zip   what a person copies to the Kobo's drive:
#                           .kobo/KoboRoot.tgz
#                           .adds/bookorbit/... (from packaging/adds/bookorbit/)
#
# The `uninstall` marker and config.ini.example are not packaged here: the
# library embeds them and writes them on first load (src/nickel/install.c).
# config.ini itself is never packaged, so reinstalling never touches the login.
foreach(var LIBRARY ASSETS OUTDIR)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "package-koboroot.cmake: ${var} is required")
    endif()
endforeach()
if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "package-koboroot.cmake: library not found: ${LIBRARY}")
endif()
file(GLOB_RECURSE real_config "${ASSETS}/*config.ini")
if(real_config)
    message(FATAL_ERROR "package-koboroot.cmake: refusing to package a real config.ini: ${real_config}")
endif()

get_filename_component(OUTDIR "${OUTDIR}" ABSOLUTE)
set(stage "${OUTDIR}/package-stage")
set(koboroot "${OUTDIR}/KoboRoot.tgz")
set(zip "${OUTDIR}/Smalt-qt5.zip")
file(REMOVE_RECURSE "${stage}")
file(REMOVE "${koboroot}" "${zip}")
set(lib_dir "${stage}/system/usr/local/Kobo/imageformats")
file(MAKE_DIRECTORY "${lib_dir}" "${stage}/zip/.kobo" "${stage}/zip/.adds")
configure_file("${LIBRARY}" "${lib_dir}/libsmalt.so" COPYONLY)

if(STRIP)
    execute_process(COMMAND "${STRIP}" --strip-unneeded "${lib_dir}/libsmalt.so" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "strip failed: ${result}")
    endif()
endif()

find_program(SMALT_TAR NAMES tar REQUIRED)
execute_process(
    COMMAND "${SMALT_TAR}" --format=gnu --owner=0 --group=0 --numeric-owner --sort=name
            --mode=u+rwX,go+rX,go-w -czf "${koboroot}" usr
    WORKING_DIRECTORY "${stage}/system"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tar failed: ${result}")
endif()

configure_file("${koboroot}" "${stage}/zip/.kobo/KoboRoot.tgz" COPYONLY)
file(COPY "${ASSETS}/bookorbit" DESTINATION "${stage}/zip/.adds")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${zip}" --format=zip .kobo .adds
    WORKING_DIRECTORY "${stage}/zip" RESULT_VARIABLE result)
file(REMOVE_RECURSE "${stage}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "zip failed: ${result}")
endif()
message(STATUS "Wrote ${koboroot} and ${zip}")
