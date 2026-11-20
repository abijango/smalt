// The companion's screens: the shell with its five tabs and everything under
// them except Stats.
//
// Spec: Feature design ("Home screen and library", "Large-library UX",
// "Downloads", "Books removed upstream" banner), UX details and copy, Visual
// design ("Devices and scaling"), Settings screen. Implemented by task 22 in
// src/ui/screens/. Task 30 shows the shell in place of Nickel's home; until
// then it runs on the desktop (tests and previews) only.
#pragma once

#include "smalt/paths.h"

#include <QString>

#include <functional>

class QWidget;

namespace smalt {

class BookOrbitClient;
class Ledger;
class NickelBridge;
class SettingsStore;
class SyncEngine;

namespace ui {
class ComponentFactory;
}

// What the shell needs; nothing is owned. Any pointer may be null on the
// desktop (previews): the screens then show their offline or empty states.
struct ShellDeps
{
    ui::ComponentFactory *kit = nullptr;
    BookOrbitClient *client = nullptr;
    Ledger *ledger = nullptr;
    NickelBridge *nickel = nullptr; // Open book, import files, Kobo home, toasts.
    SettingsStore *settings = nullptr;
    SyncEngine *sync = nullptr; // Status line, "Sync now".
    Paths paths = Paths::device();

    // Task 23's widgets. Null: the shell shows a placeholder for the Stats tab
    // and no "Your reading" section on book detail.
    std::function<QWidget *(QWidget *parent)> statsTab;
    std::function<QWidget *(const QString &contentId, QWidget *parent)> yourReading;
};

// The companion's top-level widget: header, status line, the five tabs (Home,
// Library, Series, Stats, More) and their screens. Owned by `parent`.
QWidget *createCompanionShell(const ShellDeps &deps, QWidget *parent);

} // namespace smalt
