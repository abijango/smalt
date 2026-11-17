# Owned by task 13. UI kit tests (see docs/tasks/13-ui-kit.md, "Acceptance tests").
#   ui_kit.palette      palette swap replaces every teal use; colour only where allowed
#   ui_kit.scaling      millimetre layout is identical on 6" and 7"; touch targets >= 9 mm
#   ui_kit.rules        dialog border, rules, refresh requests, taps, no animations
#   ui_kit.screenshots  each component in both palettes vs golden PNGs in golden/
set(SMALT_KIT_DEFS
    SMALT_KIT_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    SMALT_KIT_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

smalt_add_qtest(ui_kit.palette SOURCES tst_palette.cpp DEPS smalt_ui_kit)
smalt_add_qtest(ui_kit.scaling SOURCES tst_scaling.cpp DEPS smalt_ui_kit)
smalt_add_qtest(ui_kit.rules SOURCES tst_rules.cpp DEPS smalt_ui_kit)
smalt_add_qtest(ui_kit.screenshots SOURCES tst_screenshots.cpp DEPS smalt_ui_kit)

if(SMALT_TARGET STREQUAL "desktop")
    foreach(t palette scaling rules screenshots)
        target_compile_definitions(test_ui_kit_${t} PRIVATE ${SMALT_KIT_DEFS})
    endforeach()
    # Rewrites tests/ui/kit/golden/ from the current rendering.
    add_custom_target(ui_kit_update_goldens
        COMMAND ${CMAKE_COMMAND} -E env SMALT_UPDATE_GOLDENS=1 QT_QPA_PLATFORM=offscreen
                $<TARGET_FILE:test_ui_kit_screenshots>
        DEPENDS test_ui_kit_screenshots
        COMMENT "Regenerating UI kit golden screenshots"
        VERBATIM)
endif()
