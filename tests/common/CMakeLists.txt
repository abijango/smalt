# Owned by task 00.
smalt_add_qtest(common.log SOURCES tst_log.cpp DEPS smalt_common)
smalt_add_qtest(common.paths SOURCES tst_paths.cpp DEPS smalt_common)
smalt_add_qtest(common.interfaces SOURCES tst_interfaces.cpp DEPS smalt_common Qt5::Gui)
smalt_add_qtest(common.settings SOURCES tst_settings.cpp DEPS smalt_common)
