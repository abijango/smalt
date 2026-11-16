# libsmalt.so: the NickelHook mod. Kobo target only (Qt 5.2 SDK image).
#
# Shared by tasks 20 (recorder hooks), 21 (Wi-Fi and sleep hold), 30 (home
# takeover) and 31 (open-book hook). Sources are picked up by glob so those
# tasks can add files without editing this list. plugin.cpp (scaffold) holds
# the NickelHook declaration; hooks and dlsyms are added there.
set(nickelhook "${PROJECT_SOURCE_DIR}/toolchains/nickelhook")

# NickelHook's Qt plugin shell. moc reads nhplugin.json next to the header,
# so both go into the build folder (as Kobalt does).
configure_file("${nickelhook}/nhplugin.h" "${CMAKE_CURRENT_BINARY_DIR}/nhplugin.h" COPYONLY)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nhplugin.json" "{\"Keys\":[\"nh.smalt\"]}\n")

file(GLOB smalt_nickel_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(smalt MODULE
    ${smalt_nickel_sources}
    "${nickelhook}/nh.c"
    "${CMAKE_CURRENT_BINARY_DIR}/nhplugin.h")
# GNU extensions: the NickelHook() macro uses a compound literal and
# designated initialisers, as NickelMenu does.
set_target_properties(smalt PROPERTIES PREFIX "lib" OUTPUT_NAME "smalt" CXX_EXTENSIONS ON)
target_include_directories(smalt PRIVATE "${nickelhook}" "${CMAKE_CURRENT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/include")
target_compile_definitions(smalt PRIVATE NH_VERSION="${SMALT_VERSION}")
set_source_files_properties("${nickelhook}/nh.c" PROPERTIES COMPILE_OPTIONS "-std=gnu11")

get_property(smalt_modules GLOBAL PROPERTY SMALT_MODULES)
find_package(Threads REQUIRED)
target_link_libraries(smalt PRIVATE ${smalt_modules} Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Sql
    Threads::Threads dl)
target_link_options(smalt PRIVATE -Wl,--no-undefined
    -Wl,-rpath,/usr/local/Kobo -Wl,-rpath,/usr/local/Qt-5.2.1-arm/lib)
