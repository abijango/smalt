// The Stats tab and the "Your reading" section as widgets: they load, build
// (stats_screens.h) and rebuild when opened and after a sync. Nothing here
// updates on a timer (Stats "Rules": nothing updates live).
//
// The public entry points are createStatsTab() and createYourReadingSection()
// in include/smalt/stats.h; this header adds the clock and time zone seams the
// tests use.
#pragma once

#include "smalt/stats.h"
#include "smalt/sync.h"
#include "stats/aggregate.h"
#include "stats/snapshot.h"

#include <QDateTime>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QWidget>

#include <functional>

class QVBoxLayout;

namespace smalt {
namespace ui {
namespace stats {

struct StatsOptions
{
    std::function<QDateTime()> now; // Default: the system clock.
    smalt::stats::ZoneOffsetFn zone; // Default: the device's local zone.
};

// The dashboard is fetched when the tab opens if the cached copy is older than
// this, and always after a sync that got through.
constexpr int kDashboardStaleSeconds = 3600;

class StatsTab : public QWidget
{
    Q_OBJECT

public:
    StatsTab(const StatsDeps &deps, const StatsOptions &options, QWidget *parent);
    ~StatsTab() override;

    int currentChip() const { return chip_; }

    // The screen on show now (owned by the tab; replaced on every rebuild).
    QWidget *page() const { return page_; }

    QWidget *chips() const { return chips_; }

    // Rebuilds the current screen from the ledger and the cached dashboard.
    void reload();

    bool fetching() const { return fetching_; }

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onChipToggled(int index, bool selected);
    void onSyncFinished(const smalt::SyncResult &result);

private:
    void showChip(int index);
    void fetchDashboard();
    void onDashboard(bool ok, const QJsonObject &body, bool rebuildAnyway);
    bool cacheIsStale();
    QDateTime now() const;
    smalt::stats::ZoneOffsetFn zone() const;
    void loadCovers(const smalt::stats::StatsSnapshot &snapshot);
    void onThumbnail(qint64 bookId, const QByteArray &bytes);

    StatsDeps deps_;
    StatsOptions options_;
    QVBoxLayout *layout_ = nullptr;
    QWidget *chips_ = nullptr;
    QWidget *page_ = nullptr;
    int chip_ = 0;
    bool fetching_ = false;
    bool dirty_ = false;
    QHash<qint64, QPixmap> covers_;
    QSet<qint64> thumbRequested_;
    int thumbPending_ = 0;
};

// "Your reading" on book detail. Hides itself while the book has no reading
// on this Kobo.
class YourReadingSection : public QWidget
{
    Q_OBJECT

public:
    YourReadingSection(const StatsDeps &deps, const QString &contentId, const StatsOptions &options,
                       QWidget *parent);

    void reload();

    QWidget *content() const { return content_; }

private slots:
    void onSyncFinished(const smalt::SyncResult &result);

private:
    StatsDeps deps_;
    StatsOptions options_;
    QString contentId_;
    QVBoxLayout *layout_ = nullptr;
    QWidget *content_ = nullptr;
};

QWidget *createStatsTab(const StatsDeps &deps, const StatsOptions &options, QWidget *parent);
QWidget *createYourReadingSection(const StatsDeps &deps, const QString &contentId,
                                  const StatsOptions &options, QWidget *parent);

} // namespace stats
} // namespace ui
} // namespace smalt
