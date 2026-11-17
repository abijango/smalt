# Owned by task 13. Desktop preview of the UI kit (added from src/ui/kit/CMakeLists.txt).
add_executable(smalt_ui_preview main.cpp)
target_link_libraries(smalt_ui_preview PRIVATE smalt_ui_kit_gallery Qt5::Widgets)
target_compile_options(smalt_ui_preview PRIVATE ${SMALT_WARNING_OPTIONS})
