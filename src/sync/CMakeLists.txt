# Owned by task 21 (see docs/tasks/). Add sources to SOURCES; the scaffold
# stub can go once real code is here.
smalt_add_module(smalt_sync
    SOURCES
        sync_stub.cpp
    DEPS smalt_outbox smalt_bookorbit smalt_safety)
