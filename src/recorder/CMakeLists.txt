# Owned by task 20 (docs/tasks/20-recorder.md).
smalt_add_module(smalt_recorder
    SOURCES
        recorder.cpp
        recorder_clock.cpp
        recorder_clock.h
        recorder_internal.h
    DEPS smalt_ledger)
