// Stats: the Stats tab (Overview, Patterns, Year) and the "Your reading"
// section on book detail.
//
// Spec: Stats (all), Visual design ("Devices and scaling", "Monochrome
// palette"), UX details ("E Ink rules"). Implemented by task 23 in src/stats/
// and src/ui/screens/stats/; the composition root passes these to the shell
// through ShellDeps::statsTab and ShellDeps::yourReading (screens.h).
#pragma once

#include <QString>

class QWidget;

namespace smalt {

class BookOrbitClient;
class Ledger;
class SyncEngine;

namespace ui {
class ComponentFactory;
}

// What Stats needs; nothing is owned. `client` may be null (offline): the
// "All devices" sections then show the cached dashboard, or nothing.
struct StatsDeps
{
    ui::ComponentFactory *kit = nullptr;
    BookOrbitClient *client = nullptr; // Dashboard (plugin/catalog/dashboard).
    Ledger *ledger = nullptr; // This Kobo's sessions and page events; dashboard cache.
    SyncEngine *sync = nullptr; // Redraw after a sync (finished()).
};

QWidget *createStatsTab(const StatsDeps &deps, QWidget *parent);
QWidget *createYourReadingSection(const StatsDeps &deps, const QString &contentId, QWidget *parent);

} // namespace smalt
