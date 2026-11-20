# Owned by task 23: Stats tests (docs/tasks/23-stats.md, "Acceptance tests").
#   stats.aggregate    minutes per day, midnight splits, sleep gaps, time zones, empty ledger
#   stats.merge        unsent minutes appear today and drop once the server has them
#   stats.forecast     the forecast formula and the fallback under three sessions
#   stats.dashboard    parsing, missing fields hide widgets, the remote_cache copy, offline
#   stats.tab          the tab as a widget: chips, redraw after a sync, the mock server
#   stats.book         "Your reading" from a real ledger
#   stats.screenshots  the screens on all three devices vs golden PNGs in golden/
#
# The tab test starts tools/mock-bookorbit (python3 with fastapi and uvicorn, see
# tools/mock-bookorbit/requirements.txt), like the client tests.
set(SMALT_STATS_DEFS
    SMALT_STATS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    SMALT_STATS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    SMALT_KIT_FONT_DIR="${PROJECT_SOURCE_DIR}/tests/ui/kit/fonts"
    SMALT_MOCK_DIR="${PROJECT_SOURCE_DIR}/tools/mock-bookorbit")

smalt_add_qtest(stats.aggregate SOURCES tst_aggregate.cpp test_support.h
    DEPS smalt_stats smalt_ui_kit smalt_ledger)
smalt_add_qtest(stats.merge SOURCES tst_merge.cpp test_support.h
    DEPS smalt_stats smalt_ui_kit smalt_ledger)
smalt_add_qtest(stats.forecast SOURCES tst_forecast.cpp test_support.h
    DEPS smalt_stats smalt_ui_kit smalt_ledger)
smalt_add_qtest(stats.dashboard SOURCES tst_dashboard.cpp test_support.h
    DEPS smalt_stats smalt_ui_kit smalt_ledger)
smalt_add_qtest(stats.tab SOURCES tst_tab.cpp test_support.h
    DEPS smalt_ui_stats smalt_bookorbit smalt_ledger)
smalt_add_qtest(stats.book SOURCES tst_book.cpp test_support.h
    DEPS smalt_ui_stats smalt_ledger)
smalt_add_qtest(stats.screenshots SOURCES tst_screenshots.cpp test_support.h
    DEPS smalt_ui_stats smalt_ledger)

if(SMALT_TARGET STREQUAL "desktop")
    foreach(t aggregate merge forecast dashboard tab book screenshots)
        target_compile_definitions(test_stats_${t} PRIVATE ${SMALT_STATS_DEFS})
    endforeach()
    # Rewrites tests/stats/golden/ from the current rendering.
    add_custom_target(stats_update_goldens
        COMMAND ${CMAKE_COMMAND} -E env SMALT_UPDATE_GOLDENS=1 QT_QPA_PLATFORM=offscreen
                $<TARGET_FILE:test_stats_screenshots>
        DEPENDS test_stats_screenshots
        COMMENT "Regenerating Stats golden screenshots"
        VERBATIM)
endif()
