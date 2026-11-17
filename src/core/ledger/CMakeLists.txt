# Owned by task 10 (docs/tasks/10-ledger.md). ledger_internal.h is shared with
# src/core/outbox/ only; other modules use include/smalt/ledger.h.
smalt_add_module(smalt_ledger
    SOURCES
        ledger.cpp
        ledger_files.cpp
        ledger_files.h
        ledger_internal.h
        schema.cpp
    DEPS Qt5::Sql)
