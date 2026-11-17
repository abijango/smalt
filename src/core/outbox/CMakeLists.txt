# Owned by task 10 (docs/tasks/10-ledger.md). The outbox is a table in the
# ledger file; it reaches the ledger's connection through
# ../ledger/ledger_internal.h.
smalt_add_module(smalt_outbox
    SOURCES
        outbox.cpp
    DEPS smalt_ledger)
