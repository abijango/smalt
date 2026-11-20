# Owned by task 11: the BookOrbit client (docs/tasks/11-bookorbit-client.md).
smalt_add_module(smalt_bookorbit
    SOURCES
        client_config.cpp
        page_stats_uploader.cpp
        partial_md5.cpp
        qt_bookorbit_client.cpp
        transfer.cpp
    DEPS Qt5::Network)

# Other modules and the tests reach this module's own headers as
# "bookorbit/<header>.h" (client_config.h, qt_bookorbit_client.h, ...).
target_include_directories(smalt_bookorbit PUBLIC "${PROJECT_SOURCE_DIR}/src")
