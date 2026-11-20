# Owned by task 23. Stats aggregation (pure functions), the snapshot the
# screens draw, and BookOrbit's dashboard parsing. The dashboard files live in
# src/bookorbit/ (new files, task 23's) but are compiled here because
# src/bookorbit/CMakeLists.txt is task 11's.
smalt_add_module(smalt_stats
    SOURCES
        aggregate.cpp
        format.cpp
        snapshot.cpp
        ${PROJECT_SOURCE_DIR}/src/bookorbit/dashboard.cpp
    DEPS Qt5::Core)

# Includes: "bookorbit/dashboard.h" and "stats/aggregate.h".
target_include_directories(smalt_stats PUBLIC "${PROJECT_SOURCE_DIR}/src")
