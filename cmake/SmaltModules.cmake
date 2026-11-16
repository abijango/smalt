# Helpers that keep each task's build files inside its own folder.
#
#   smalt_add_module(<name> SOURCES <files...> [DEPS <targets...>])
#       A static library for one module under src/. It sees include/ and
#       links smalt_common. Every module is also linked into libsmalt.so on
#       the Kobo target, so a new module needs no change outside its folder.
#
#   smalt_add_qtest(<name> SOURCES <files...> [DEPS <targets...>])
#       A Qt Test executable registered with CTest as <name>. Runs headless
#       (QT_QPA_PLATFORM=offscreen) with a per-test temporary HOME.
#
#   smalt_add_subdirectories()
#       Adds every direct subfolder that has a CMakeLists.txt, so parallel
#       tasks that own sibling folders (src/features/<x>/) never edit a
#       shared list.

define_property(GLOBAL PROPERTY SMALT_MODULES
    BRIEF_DOCS "Static libraries linked into libsmalt.so"
    FULL_DOCS "Appended by smalt_add_module().")

set(SMALT_WARNING_OPTIONS -Wall -Wextra -Wno-unused-parameter)

function(smalt_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "smalt_add_module(${name}) needs SOURCES")
    endif()
    add_library(${name} STATIC ${ARG_SOURCES})
    target_include_directories(${name} PUBLIC "${PROJECT_SOURCE_DIR}/include")
    target_compile_options(${name} PRIVATE ${SMALT_WARNING_OPTIONS})
    if(NOT name STREQUAL "smalt_common")
        target_link_libraries(${name} PUBLIC smalt_common)
    endif()
    if(ARG_DEPS)
        target_link_libraries(${name} PUBLIC ${ARG_DEPS})
    endif()
    set_property(GLOBAL APPEND PROPERTY SMALT_MODULES ${name})
endfunction()

function(smalt_add_qtest name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS" ${ARGN})
    if(NOT SMALT_TARGET STREQUAL "desktop")
        return()
    endif()
    string(REPLACE "." "_" target "test_${name}")
    add_executable(${target} ${ARG_SOURCES})
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include" "${PROJECT_SOURCE_DIR}/tests")
    target_compile_options(${target} PRIVATE ${SMALT_WARNING_OPTIONS})
    target_link_libraries(${target} PRIVATE Qt5::Test ${ARG_DEPS})
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SMALT_TEST=1"
        TIMEOUT 120)
endfunction()

macro(smalt_add_subdirectories)
    file(GLOB _smalt_children LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/*")
    list(SORT _smalt_children)
    foreach(_smalt_child IN LISTS _smalt_children)
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_smalt_child}"
                AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_smalt_child}/CMakeLists.txt")
            add_subdirectory(${_smalt_child})
        endif()
    endforeach()
endmacro()
