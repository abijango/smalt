# Owned by task 23. The Stats tab (Overview, Patterns, Year) and the "Your
# reading" section of book detail, drawn from the kit's tokens. The shell does
# not link this module: it gets the widgets through ShellDeps::statsTab and
# ShellDeps::yourReading, which the composition root fills from
# createStatsTab() and createYourReadingSection() (include/smalt/stats.h).
smalt_add_module(smalt_ui_stats
    SOURCES
        stats_screens.cpp
        stats_tab.cpp
        widgets/blocks.cpp
        widgets/charts.cpp
    DEPS smalt_stats smalt_ui_kit Qt5::Widgets)

# Tests reach the internal headers as "stats_tab.h", "stats_screens.h" and "widgets/...".
target_include_directories(smalt_ui_stats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
