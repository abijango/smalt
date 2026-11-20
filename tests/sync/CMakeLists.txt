# Owned by task 21: the sync engine's acceptance tests
# (docs/tasks/21-sync-engine.md). They start tools/mock-bookorbit like the
# client tests (python3 with fastapi and uvicorn) and reach the engine's and
# src/safety/'s internal headers for the clock seam.
smalt_add_qtest(sync.engine
    SOURCES tst_sync_engine.cpp sync_fixture.h fake_recorder.h
    DEPS smalt_sync smalt_test_fakes)
smalt_add_qtest(sync.text SOURCES tst_sync_text.cpp DEPS smalt_sync)

foreach(_t test_sync_engine test_sync_text)
    if(TARGET ${_t})
        target_include_directories(${_t} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    endif()
endforeach()
if(TARGET test_sync_engine)
    target_compile_definitions(test_sync_engine PRIVATE
        SMALT_MOCK_DIR="${PROJECT_SOURCE_DIR}/tools/mock-bookorbit")
endif()
