# Owned by task 00 (scaffold). Shared by every module.
configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/smalt/version.h" @ONLY)

smalt_add_module(smalt_common
    SOURCES
        log.cpp
        paths.cpp
        settings.cpp
        # QObject interfaces: their meta-objects are compiled here, once.
        "${PROJECT_SOURCE_DIR}/include/smalt/nickel.h"
        "${PROJECT_SOURCE_DIR}/include/smalt/sync.h"
        "${PROJECT_SOURCE_DIR}/include/smalt/settings.h"
        "${PROJECT_SOURCE_DIR}/include/smalt/recorder.h"
    DEPS Qt5::Core)

target_include_directories(smalt_common PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
find_package(Threads REQUIRED)
target_link_libraries(smalt_common PUBLIC Threads::Threads)
