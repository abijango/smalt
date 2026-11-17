# Owned by task 13. The UI kit: tokens, palettes, mm layout and components.
# Includes: "kit/factory.h" etc. resolve through the PUBLIC src/ui directory.
smalt_add_module(smalt_ui_kit
    SOURCES
        theme.cpp
        components.cpp
        factory.cpp
        gallery.cpp
    DEPS Qt5::Core Qt5::Gui Qt5::Widgets)
target_include_directories(smalt_ui_kit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
