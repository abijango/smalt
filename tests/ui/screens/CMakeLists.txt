# Owned by task 22: acceptance tests from docs/tasks/22-screens.md.
#   ui_screens.screenshots  each screen on the three device profiles vs golden PNGs in golden/
#   ui_screens.behaviour    paging, jump-to-letter, chips, search, tabs, device layouts
#   ui_screens.offline      cached pages offline, undownloaded items unavailable
#   ui_screens.download     queue, resume, save path, book_map, import, Open (against the mock server)
#   ui_screens.settings     settings round trip, config.ini writer, Test connection
#   ui_screens.copy         the strings against the spec's Copy table
set(SMALT_SCREENS_DEFS
    SMALT_KIT_TEST_DIR="${PROJECT_SOURCE_DIR}/tests/ui/kit"
    SMALT_SCREENS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    SMALT_MOCK_DIR="${PROJECT_SOURCE_DIR}/tools/mock-bookorbit"
    SMALT_REPO_DIR="${PROJECT_SOURCE_DIR}")

set(SMALT_SCREENS_TEST_DEPS smalt_ui_screens smalt_test_fakes)

smalt_add_qtest(ui_screens.screenshots SOURCES tst_screenshots.cpp support.h
    DEPS ${SMALT_SCREENS_TEST_DEPS})
smalt_add_qtest(ui_screens.behaviour SOURCES tst_behaviour.cpp support.h
    DEPS ${SMALT_SCREENS_TEST_DEPS})
smalt_add_qtest(ui_screens.offline SOURCES tst_offline.cpp support.h
    DEPS ${SMALT_SCREENS_TEST_DEPS})
smalt_add_qtest(ui_screens.download SOURCES tst_download.cpp support.h
    DEPS ${SMALT_SCREENS_TEST_DEPS})
smalt_add_qtest(ui_screens.settings SOURCES tst_settings.cpp support.h
    DEPS ${SMALT_SCREENS_TEST_DEPS})
smalt_add_qtest(ui_screens.copy SOURCES tst_copy.cpp support.h
    DEPS ${SMALT_SCREENS_TEST_DEPS})

if(SMALT_TARGET STREQUAL "desktop")
    foreach(t screenshots behaviour offline download settings copy)
        target_compile_definitions(test_ui_screens_${t} PRIVATE ${SMALT_SCREENS_DEFS})
        # The tests reach the screens' own headers as "screens/<header>.h".
        target_include_directories(test_ui_screens_${t} PRIVATE "${PROJECT_SOURCE_DIR}/src/ui")
    endforeach()
    # Rewrites tests/ui/screens/golden/ from the current rendering.
    add_custom_target(ui_screens_update_goldens
        COMMAND ${CMAKE_COMMAND} -E env SMALT_UPDATE_GOLDENS=1 QT_QPA_PLATFORM=offscreen
                $<TARGET_FILE:test_ui_screens_screenshots>
        DEPENDS test_ui_screens_screenshots
        COMMENT "Regenerating screens golden screenshots"
        VERBATIM)
endif()
