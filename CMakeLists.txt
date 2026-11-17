cmake_minimum_required(VERSION 3.22)
project(Smalt VERSION 0.1.0 LANGUAGES C CXX)

# SMALT_TARGET picks what gets built (see CLAUDE.md, "Build and test"):
#   desktop  every module except src/nickel/ as static libraries, plus the tests,
#            on Linux x86_64 with the system Qt 5.
#   kobo     the same modules cross-compiled, plus src/nickel/ and NickelHook,
#            linked into libsmalt.so and packaged as out/KoboRoot.tgz.
#            Use ./build-kobo.sh; it runs inside the pinned Kobo Qt5 SDK image.
set(SMALT_TARGET "desktop" CACHE STRING "Build target: desktop or kobo")
set_property(CACHE SMALT_TARGET PROPERTY STRINGS desktop kobo)
if(NOT SMALT_TARGET MATCHES "^(desktop|kobo)$")
    message(FATAL_ERROR "SMALT_TARGET must be desktop or kobo, not '${SMALT_TARGET}'")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# The Kobo Qt5 SDK is Qt 5.2 with GCC 4.9's C++ library. Code must stay
# within that: C++17 language features, but no <optional>, <variant>,
# <string_view> or <any>, and no Qt API newer than 5.2. See BUILDING.md.
find_package(Qt5 5.2 REQUIRED COMPONENTS Core Gui Widgets Network Sql)

include(cmake/SmaltVersion.cmake)
include(cmake/SmaltModules.cmake)

if(SMALT_TARGET STREQUAL "desktop")
    find_package(Qt5 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

add_subdirectory(src)

add_subdirectory(packaging)
if(SMALT_TARGET STREQUAL "desktop")
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()
