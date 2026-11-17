# Owned by task 11: acceptance tests from docs/tasks/11-bookorbit-client.md.
#
# The client tests start tools/mock-bookorbit (a Python FastAPI server) on a
# free port themselves, so the machine needs python3 with fastapi and uvicorn
# (tools/mock-bookorbit/requirements.txt). They fail, not skip, when it is missing.
smalt_add_qtest(bookorbit.partial_md5 SOURCES tst_partial_md5.cpp DEPS smalt_bookorbit)
smalt_add_qtest(bookorbit.client_config SOURCES tst_client_config.cpp DEPS smalt_bookorbit)
smalt_add_qtest(bookorbit.client SOURCES tst_client.cpp DEPS smalt_bookorbit)
smalt_add_qtest(bookorbit.download SOURCES tst_download.cpp DEPS smalt_bookorbit)
smalt_add_qtest(bookorbit.page_stats SOURCES tst_page_stats.cpp DEPS smalt_bookorbit)
smalt_add_qtest(bookorbit.https SOURCES tst_https.cpp DEPS smalt_bookorbit)

foreach(_t client download page_stats https)
    if(TARGET test_bookorbit_${_t})
        target_compile_definitions(test_bookorbit_${_t} PRIVATE
            SMALT_MOCK_DIR="${PROJECT_SOURCE_DIR}/tools/mock-bookorbit")
    endif()
endforeach()

# The mock's own tests (pytest).
if(SMALT_TARGET STREQUAL "desktop")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME bookorbit.mock_pytest
            COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider
                    "${PROJECT_SOURCE_DIR}/tools/mock-bookorbit/tests"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/tools/mock-bookorbit")
        set_tests_properties(bookorbit.mock_pytest PROPERTIES
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1"
            TIMEOUT 120)
    else()
        message(WARNING "python3 not found: bookorbit.mock_pytest and the client tests that start the mock will not run")
    endif()
endif()
