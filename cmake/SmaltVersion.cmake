# Version string compiled into the mod and written to the log at startup:
# the project version plus the short commit, with "-dirty" for uncommitted
# changes. Falls back to the project version outside a git checkout.
#
# build-kobo.sh passes SMALT_VERSION_OVERRIDE, computed on the host, because
# git inside the SDK container may not be able to read the checkout.
set(SMALT_VERSION_OVERRIDE "" CACHE STRING "Version string to use instead of git describe")
set(SMALT_VERSION "${PROJECT_VERSION}")
find_package(Git QUIET)
if(SMALT_VERSION_OVERRIDE)
    set(SMALT_VERSION "${SMALT_VERSION_OVERRIDE}")
elseif(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=8
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE smalt_git_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE smalt_git_result)
    if(smalt_git_result EQUAL 0 AND smalt_git_describe)
        set(SMALT_VERSION "${PROJECT_VERSION}+${smalt_git_describe}")
    endif()
endif()
message(STATUS "Smalt version: ${SMALT_VERSION} (target: ${SMALT_TARGET})")
