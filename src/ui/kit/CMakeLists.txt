# Owned by task 13. The UI kit: tokens, palettes, mm layout and components.
# Includes: "kit/factory.h" etc. resolve through the PUBLIC src/ui directory.
smalt_add_module(smalt_ui_kit
    SOURCES
        theme.cpp
        components.cpp
        factory.cpp
    DEPS Qt5::Core Qt5::Gui Qt5::Widgets)
target_include_directories(smalt_ui_kit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Sample pages for the preview app and the screenshot tests. Desktop only, and
# deliberately not a smalt_add_module(), so it is never linked into libsmalt.so.
if(SMALT_TARGET STREQUAL "desktop")
    add_library(smalt_ui_kit_gallery STATIC gallery.cpp)
    target_compile_options(smalt_ui_kit_gallery PRIVATE ${SMALT_WARNING_OPTIONS})
    target_link_libraries(smalt_ui_kit_gallery PUBLIC smalt_ui_kit)
endif()
