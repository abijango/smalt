# Owned by task 12. Crash safety acceptance tests (docs/tasks/12-crash-safety.md),
# on a fake clock (fake_clock.h) and a temporary user partition. They reach
# src/safety/'s internal headers for the clock seam.
smalt_add_qtest(safety.startup SOURCES tst_startup.cpp fake_clock.h DEPS smalt_safety)
smalt_add_qtest(safety.watchdog SOURCES tst_watchdog.cpp fake_clock.h DEPS smalt_safety)
smalt_add_qtest(safety.nickel_ready SOURCES tst_nickel_ready.cpp fake_clock.h
    DEPS smalt_safety smalt_test_fakes)

foreach(_safety_test IN ITEMS test_safety_startup test_safety_watchdog test_safety_nickel_ready)
    target_include_directories(${_safety_test} PRIVATE "${PROJECT_SOURCE_DIR}/src")
endforeach()
