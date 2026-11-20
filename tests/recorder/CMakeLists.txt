# Owned by task 20: the brief's acceptance tests (docs/tasks/20-recorder.md).
smalt_add_qtest(recorder.recorder SOURCES tst_recorder.cpp fake_recorder_clock.h
    DEPS smalt_recorder smalt_test_fakes)
