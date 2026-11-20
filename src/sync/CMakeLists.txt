# Owned by task 21 (docs/tasks/21-sync-engine.md): the sync engine.
smalt_add_module(smalt_sync
    SOURCES
        sync_internal.h
        sync_engine.cpp
        sync_text.h
        sync_text.cpp
    DEPS smalt_outbox smalt_bookorbit smalt_safety Qt5::Core)

# The engine reaches src/safety/'s clock seam ("safety/clock.h") and the
# tests reach this module's internal headers as "sync/<header>.h".
target_include_directories(smalt_sync PUBLIC "${PROJECT_SOURCE_DIR}/src")
