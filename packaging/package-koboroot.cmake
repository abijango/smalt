# Builds KoboRoot.tgz. Run with cmake -P:
#
#   cmake -DLIBRARY=<libsmalt.so> -DASSETS=<packaging/adds> -DOUTPUT=<KoboRoot.tgz>
#         [-DSTRIP=<arm strip>] -P package-koboroot.cmake
#
# Layout (the Kobo extracts KoboRoot.tgz at / on the next boot):
#   usr/local/Kobo/imageformats/libsmalt.so     the NickelHook mod
#   mnt/onboard/.adds/bookorbit/uninstall       delete to uninstall
#   mnt/onboard/.adds/bookorbit/config.ini.example
#   mnt/onboard/.adds/bookorbit/certs/ca-bundle.pem
#
# The library path matches NickelHook's and Kobalt's Qt5 packages. Shipping
# .adds files inside KoboRoot.tgz is what NickelMenu does. config.ini itself is
# never packaged, so reinstalling never overwrites the user's login.
foreach(var LIBRARY ASSETS OUTPUT)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "package-koboroot.cmake: ${var} is required")
    endif()
endforeach()
if(NOT EXISTS "${LIBRARY}")
    message(FATAL_ERROR "package-koboroot.cmake: library not found: ${LIBRARY}")
endif()
if(EXISTS "${ASSETS}/bookorbit/config.ini")
    message(FATAL_ERROR "package-koboroot.cmake: refusing to package a real config.ini")
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
set(stage "${output_dir}/koboroot-stage")
file(REMOVE_RECURSE "${stage}")
set(lib_dir "${stage}/usr/local/Kobo/imageformats")
set(adds_dir "${stage}/mnt/onboard/.adds")
file(MAKE_DIRECTORY "${lib_dir}" "${adds_dir}")
configure_file("${LIBRARY}" "${lib_dir}/libsmalt.so" COPYONLY)
file(COPY "${ASSETS}/bookorbit" DESTINATION "${adds_dir}")

if(STRIP)
    execute_process(COMMAND "${STRIP}" --strip-unneeded "${lib_dir}/libsmalt.so" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "strip failed: ${result}")
    endif()
endif()

find_program(SMALT_TAR NAMES tar REQUIRED)
file(REMOVE "${OUTPUT}")
execute_process(
    COMMAND "${SMALT_TAR}" --format=gnu --owner=0 --group=0 --numeric-owner --sort=name
            --mode=u+rwX,go+rX,go-w -czf "${OUTPUT}" usr mnt
    WORKING_DIRECTORY "${stage}"
    RESULT_VARIABLE result)
file(REMOVE_RECURSE "${stage}")
if(NOT result EQUAL 0)
    message(FATAL_ERROR "tar failed: ${result}")
endif()
message(STATUS "Wrote ${OUTPUT}")
