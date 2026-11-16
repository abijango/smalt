# Checks the layout of a KoboRoot.tgz. Run with cmake -P:
#   cmake -DARCHIVE=<KoboRoot.tgz> -P check-koboroot.cmake
# Used by the desktop test packaging.koboroot_layout and by build-kobo.sh.
if(NOT EXISTS "${ARCHIVE}")
    message(FATAL_ERROR "No archive at ${ARCHIVE}")
endif()
find_program(SMALT_TAR NAMES tar REQUIRED)
execute_process(COMMAND "${SMALT_TAR}" -tzvf "${ARCHIVE}"
    OUTPUT_VARIABLE listing RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot list ${ARCHIVE}")
endif()

set(required
    "usr/local/Kobo/imageformats/libsmalt.so"
    "mnt/onboard/.adds/bookorbit/uninstall"
    "mnt/onboard/.adds/bookorbit/config.ini.example"
    "mnt/onboard/.adds/bookorbit/certs/ca-bundle.pem")
foreach(member IN LISTS required)
    string(FIND "${listing}" " ${member}\n" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "KoboRoot.tgz is missing ${member}\n${listing}")
    endif()
endforeach()
if(listing MATCHES "config\\.ini\n")
    message(FATAL_ERROR "KoboRoot.tgz must not contain config.ini")
endif()
string(REPLACE "\n" ";" lines "${listing}")
foreach(line IN LISTS lines)
    if(line AND NOT line MATCHES " (0/0|root/root) ")
        message(FATAL_ERROR "KoboRoot.tgz members must be owned by root (uid 0, gid 0): ${line}")
    endif()
endforeach()
message(STATUS "KoboRoot.tgz layout OK")
