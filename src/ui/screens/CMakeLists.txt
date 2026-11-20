# Owned by task 22 (docs/tasks/22-screens.md): the companion shell and every
# screen except Stats (src/ui/screens/stats/, task 23). Built only from the UI
# kit's components; the Stats tab and "Your reading" arrive through ShellDeps.
smalt_add_module(smalt_ui_screens
    SOURCES
        catalog.cpp
        catalog.h
        copy.h
        downloads.cpp
        downloads.h
        login_config.cpp
        login_config.h
        look.cpp
        look.h
        pages.h
        pages_book.cpp
        pages_common.cpp
        pages_common.h
        pages_home.cpp
        pages_library.cpp
        pages_search.cpp
        pages_settings.cpp
        shell.cpp
        shell.h
    DEPS smalt_ui_kit smalt_bookorbit smalt_ledger Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)
