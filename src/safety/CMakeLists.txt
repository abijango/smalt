# Owned by task 12 (docs/tasks/12-crash-safety.md). Crash safety: startup
# marker, two-strike recovery, switch files, readiness timeout, sleep
# watchdog, wait-for-Nickel and the uninstall cleanup.
smalt_add_module(smalt_safety
    SOURCES
        clock.h
        clock.cpp
        safety_internal.h
        safety.cpp
        sleep_watchdog.cpp
        nickel_ready.h
        nickel_ready.cpp
    DEPS Qt5::Core)
