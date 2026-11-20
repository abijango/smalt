#include "stats_tab.h"

#include "kit/components.h"
#include "smalt/bookorbit_client.h"
#include "smalt/ledger.h"
#include "smalt/ui_kit.h"
#include "stats/snapshot.h"
#include "stats_screens.h"
#include "widgets/blocks.h"

#include <QPointer>
#include <QShowEvent>
#include <QVBoxLayout>

namespace smalt {
namespace ui {
namespace stats {

namespace {

namespace ss = smalt::stats;

const char *const kChipKey = "stats.chip";
const char *const kChipKind = "stats-ui";
const char *const kThumbKind = "stats-thumb";

// Remembers the last chip when there is no ledger to keep it in.
int &memoryChip()
{
    static int chip = 0;
    return chip;
}

QString thumbKey(qint64 bookId)
{
    return QStringLiteral("stats.thumb.%1").arg(bookId);
}

void paintBackground(QWidget *w, const QColor &paper)
{
    w->setAutoFillBackground(true);
    QPalette pal = w->palette();
    pal.setColor(QPalette::Window, paper);
    w->setPalette(pal);
}

} // namespace

// ---- StatsTab ----

StatsTab::StatsTab(const StatsDeps &deps, const StatsOptions &options, QWidget *parent)
    : QWidget(parent), deps_(deps), options_(options)
{
    const Style style(*deps_.kit);
    paintBackground(this, style.palette.paper);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);

    // The last chip used, from the ledger (a disposable UI note) or memory.
    chip_ = 0;
    if (!deps_.ledger || !deps_.ledger->isOpen()) {
        chip_ = memoryChip();
    } else {
        QByteArray bytes;
        QDateTime when;
        if (deps_.ledger->cacheGet(QString::fromLatin1(kChipKey), &bytes, &when).ok()) {
            bool ok = false;
            const int v = QString::fromLatin1(bytes).toInt(&ok);
            if (ok)
                chip_ = v;
        }
    }
    if (chip_ < 0 || chip_ >= chipNames().size())
        chip_ = 0;

    layout_->addSpacing(style.px(0.8));
    chips_ = deps_.kit->chipRow(chipNames(), this);
    layout_->addWidget(chips_);
    layout_->addSpacing(style.px(0.8));
    layout_->addStretch(1);
    if (kit::ChipRow *row = qobject_cast<kit::ChipRow *>(chips_))
        row->setSelected(QList<int>() << chip_);
    connect(chips_, SIGNAL(chipToggled(int, bool)), this, SLOT(onChipToggled(int, bool)));

    if (deps_.sync)
        connect(deps_.sync, &SyncEngine::finished, this, &StatsTab::onSyncFinished);

    reload();
}

StatsTab::~StatsTab()
{
    // Replies that arrive later find a null QPointer and do nothing.
}

QDateTime StatsTab::now() const
{
    return options_.now ? options_.now() : QDateTime::currentDateTimeUtc();
}

ss::ZoneOffsetFn StatsTab::zone() const
{
    return options_.zone ? options_.zone : ss::systemZone();
}

void StatsTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Opened: redraw from the ledger and, if the cached dashboard is old, fetch.
    reload();
    dirty_ = false;
    if (deps_.client && cacheIsStale())
        fetchDashboard();
}

void StatsTab::onChipToggled(int index, bool selected)
{
    Q_UNUSED(selected);
    if (kit::ChipRow *row = qobject_cast<kit::ChipRow *>(chips_))
        row->setSelected(QList<int>() << index); // One chip is always on.
    showChip(index);
}

void StatsTab::showChip(int index)
{
    if (index < 0 || index >= chipNames().size())
        return;
    const bool changed = index != chip_;
    chip_ = index;
    memoryChip() = index;
    if (deps_.ledger && deps_.ledger->isOpen())
        deps_.ledger->cachePut(QString::fromLatin1(kChipKey), QString::fromLatin1(kChipKind),
                               QByteArray::number(index), now());
    if (changed || !page_)
        reload();
}

void StatsTab::reload()
{
    const ss::StatsInputs in = ss::loadInputs(deps_.ledger, deps_.kit->profile(), now(), zone());
    const ss::StatsSnapshot snapshot = ss::buildSnapshot(in);

    QWidget *page = nullptr;
    switch (chip_) {
    case 1:
        page = buildPatterns(deps_.kit, snapshot, this);
        break;
    case 2: {
        loadCovers(snapshot);
        // Room below the chips: the screen minus the shell's header and tabs.
        const Style style(*deps_.kit);
        const int chrome = style.px(13.0 + 14.0) + chips_->height() + style.px(1.6);
        page = buildYear(deps_.kit, snapshot, covers_, deps_.kit->profile().heightPx - chrome,
                         this);
        break;
    }
    case 0:
    default:
        page = buildOverview(deps_.kit, snapshot, this);
        break;
    }

    if (page_) {
        layout_->removeWidget(page_);
        delete page_;
    }
    page_ = page;
    layout_->insertWidget(3, page_);
    page_->show();
    // Lay the new page out now rather than on the next event-loop turn, so what is
    // painted next is the new screen in its place (one redraw, no half-built frame).
    if (isVisible())
        layout_->activate();
}

bool StatsTab::cacheIsStale()
{
    if (!deps_.ledger || !deps_.ledger->isOpen())
        return true;
    Dashboard d;
    QDateTime updated;
    if (!loadCachedDashboard(deps_.ledger, &d, &updated).ok() || !updated.isValid())
        return true;
    return updated.secsTo(now()) > kDashboardStaleSeconds;
}

void StatsTab::fetchDashboard()
{
    if (!deps_.client || fetching_)
        return;
    fetching_ = true;
    QPointer<StatsTab> self(this);
    deps_.client->dashboard([self](const Reply<QJsonObject> &reply) {
        if (self)
            self->onDashboard(reply.result.ok(), reply.value, true);
    });
}

void StatsTab::onDashboard(bool ok, const QJsonObject &body, bool rebuildAnyway)
{
    fetching_ = false;
    if (ok && deps_.ledger && deps_.ledger->isOpen())
        cacheDashboard(deps_.ledger, body, now());
    // Offline or refused: keep what is cached and drawn.
    if (ok || rebuildAnyway) {
        if (isVisible())
            reload();
        else
            dirty_ = true;
    }
}

void StatsTab::onSyncFinished(const smalt::SyncResult &result)
{
    // Only a sync that moved data can change what Stats shows.
    if (result.outcome != SyncOutcome::Succeeded && result.outcome != SyncOutcome::Partial)
        return;
    if (deps_.client) {
        fetchDashboard();
        return;
    }
    if (isVisible())
        reload();
    else
        dirty_ = true;
}

void StatsTab::loadCovers(const ss::StatsSnapshot &snapshot)
{
    QVector<qint64> ids;
    for (const DashboardBook &b : snapshot.finishedBooks)
        ids.append(b.id);
    if (snapshot.showYearAgo)
        ids.append(snapshot.yearAgo.book.id);
    for (qint64 id : ids) {
        if (id <= 0 || covers_.contains(id))
            continue;
        QByteArray bytes;
        QDateTime when;
        if (deps_.ledger && deps_.ledger->isOpen()
            && deps_.ledger->cacheGet(thumbKey(id), &bytes, &when).ok()) {
            QPixmap pix;
            if (pix.loadFromData(bytes)) {
                covers_.insert(id, pix);
                continue;
            }
        }
        if (deps_.client && !thumbRequested_.contains(id)) {
            thumbRequested_.insert(id);
            ++thumbPending_;
            QPointer<StatsTab> self(this);
            deps_.client->thumbnail(id, [self, id](const Reply<QByteArray> &reply) {
                if (self)
                    self->onThumbnail(id, reply.result.ok() ? reply.value : QByteArray());
            });
        }
    }
}

void StatsTab::onThumbnail(qint64 bookId, const QByteArray &bytes)
{
    if (thumbPending_ > 0)
        --thumbPending_;
    if (!bytes.isEmpty()) {
        QPixmap pix;
        if (pix.loadFromData(bytes)) {
            covers_.insert(bookId, pix);
            if (deps_.ledger && deps_.ledger->isOpen())
                deps_.ledger->cachePut(thumbKey(bookId), QString::fromLatin1(kThumbKind), bytes,
                                       now());
        }
    }
    // One redraw when the last cover of this batch is in.
    if (thumbPending_ == 0 && chip_ == 2 && isVisible())
        reload();
}

// ---- YourReadingSection ----

YourReadingSection::YourReadingSection(const StatsDeps &deps, const QString &contentId,
                                       const StatsOptions &options, QWidget *parent)
    : QWidget(parent), deps_(deps), options_(options), contentId_(contentId)
{
    const Style style(*deps_.kit);
    paintBackground(this, style.palette.paper);
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    if (deps_.sync)
        connect(deps_.sync, &SyncEngine::finished, this, &YourReadingSection::onSyncFinished);
    reload();
}

void YourReadingSection::reload()
{
    const QDateTime now = options_.now ? options_.now() : QDateTime::currentDateTimeUtc();
    const ss::ZoneOffsetFn zone = options_.zone ? options_.zone : ss::systemZone();
    const ss::BookSnapshot snapshot =
            ss::buildBookSnapshot(ss::loadBookInputs(deps_.ledger, contentId_, now, zone));

    if (content_) {
        layout_->removeWidget(content_);
        delete content_;
        content_ = nullptr;
    }
    if (!snapshot.hasReading) {
        hide(); // Nothing read on this Kobo: book detail shows no section.
        return;
    }
    content_ = buildYourReading(deps_.kit, snapshot, this);
    layout_->addWidget(content_);
    if (parentWidget())
        show(); // Back from hidden; a parentless section never opens a window of its own.
}

void YourReadingSection::onSyncFinished(const smalt::SyncResult &result)
{
    if (result.outcome == SyncOutcome::Succeeded || result.outcome == SyncOutcome::Partial)
        reload();
}

QWidget *createStatsTab(const StatsDeps &deps, const StatsOptions &options, QWidget *parent)
{
    if (!deps.kit)
        return nullptr;
    return new StatsTab(deps, options, parent);
}

QWidget *createYourReadingSection(const StatsDeps &deps, const QString &contentId,
                                  const StatsOptions &options, QWidget *parent)
{
    if (!deps.kit)
        return nullptr;
    return new YourReadingSection(deps, contentId, options, parent);
}

} // namespace stats
} // namespace ui

// ---- include/smalt/stats.h ----

QWidget *createStatsTab(const StatsDeps &deps, QWidget *parent)
{
    return ui::stats::createStatsTab(deps, ui::stats::StatsOptions(), parent);
}

QWidget *createYourReadingSection(const StatsDeps &deps, const QString &contentId, QWidget *parent)
{
    return ui::stats::createYourReadingSection(deps, contentId, ui::stats::StatsOptions(), parent);
}

} // namespace smalt
