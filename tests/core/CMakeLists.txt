# Owned by task 10: ledger and outbox tests (docs/tasks/10-ledger.md).
smalt_add_qtest(core.ledger_schema SOURCES tst_ledger_schema.cpp core_test_util.h
    DEPS smalt_ledger Qt5::Sql)
smalt_add_qtest(core.ledger_data SOURCES tst_ledger_data.cpp core_test_util.h
    DEPS smalt_outbox Qt5::Sql)
smalt_add_qtest(core.ledger_protection SOURCES tst_ledger_protection.cpp core_test_util.h
    DEPS smalt_ledger Qt5::Sql)
smalt_add_qtest(core.outbox SOURCES tst_outbox.cpp core_test_util.h
    DEPS smalt_outbox Qt5::Sql)
