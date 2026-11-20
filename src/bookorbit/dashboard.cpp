#include "dashboard.h"

#include "smalt/ledger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

// Source of every field below, at BookOrbit commit
// c77966cc529423e993fae8a298aa77048b43bba2:
//   TYPES  = packages/types/src/koreader.ts    (the dashboard response)
//   WIDGET = packages/types/src/dashboard.ts   (goal, streak, highlight shapes)
//   PLUGIN = koreader-plugin/bookorbit.koplugin/bookorbit_board_data.lua
//   SERVER = server/src/modules/koreader/koreader-catalog.service.ts,
//            server/src/modules/dashboard/dashboard-widget.service.ts and
//            dashboard-widget.calculations.ts

namespace smalt {

namespace {

bool isNumber(const QJsonValue &v)
{
    return v.isDouble();
}

bool isObject(const QJsonValue &v)
{
    return v.isObject();
}

// Whole non-negative number, or `fallback` when absent. Fractions round.
int roundedMin0(const QJsonValue &v)
{
    if (!isNumber(v))
        return 0;
    const double d = v.toDouble();
    return d <= 0 ? 0 : int(d + 0.5);
}

QDate parseDay(const QJsonValue &v)
{
    if (!v.isString())
        return QDate();
    return QDate::fromString(v.toString().left(10), QStringLiteral("yyyy-MM-dd"));
}

QString stringOf(const QJsonValue &v)
{
    return v.isString() ? v.toString() : QString();
}

// KoreaderCatalogBookListItem (TYPES 324-342). PLUGIN reads it in decorate()
// and uses id, title, authors, progressPercentage and finishedOn (PLUGIN 674,
// 729-735, 804).
DashboardBook parseBook(const QJsonObject &o)
{
    DashboardBook b;
    b.id = qint64(o.value(QStringLiteral("id")).toDouble());
    b.title = stringOf(o.value(QStringLiteral("title")));
    const QJsonValue authors = o.value(QStringLiteral("authors"));
    if (authors.isArray()) {
        const QJsonArray a = authors.toArray();
        for (int i = 0; i < a.size(); ++i)
            if (a.at(i).isString())
                b.authors.append(a.at(i).toString());
    }
    const QJsonValue progress = o.value(QStringLiteral("progressPercentage"));
    if (isNumber(progress)) {
        b.hasProgress = true;
        b.progressPercentage = progress.toDouble();
    }
    b.finishedOn = stringOf(o.value(QStringLiteral("finishedOn")));
    b.hasCover = o.value(QStringLiteral("hasCover")).toBool(false);
    return b;
}

DashboardBadge parseBadge(const QJsonObject &o, bool *ok)
{
    DashboardBadge b;
    b.key = stringOf(o.value(QStringLiteral("key")));
    b.groupKey = stringOf(o.value(QStringLiteral("groupKey")));
    b.name = stringOf(o.value(QStringLiteral("name")));
    b.description = stringOf(o.value(QStringLiteral("description")));
    const QJsonValue threshold = o.value(QStringLiteral("threshold"));
    if (isNumber(threshold)) {
        b.hasThreshold = true;
        b.threshold = threshold.toDouble();
    }
    const QJsonValue progress = o.value(QStringLiteral("currentProgress"));
    if (isNumber(progress)) {
        b.hasProgress = true;
        b.currentProgress = progress.toDouble();
    }
    // PLUGIN 512: a badge without a string name is dropped.
    *ok = !b.name.isEmpty();
    return b;
}

} // namespace

Dashboard parseDashboard(const QJsonObject &body)
{
    Dashboard d;

    // generatedAt: TYPES 522 (ISO 8601).
    const QJsonValue generated = body.value(QStringLiteral("generatedAt"));
    if (generated.isString())
        d.generatedAtUtc = QDateTime::fromString(generated.toString(), Qt::ISODate).toUTC();

    // readingYear { startDate: "yyyy-MM-dd", minutes: number[] }: TYPES 482-486.
    // SERVER (getReadingDays, service 186-199) sends 371 days (koreader-catalog.service.ts 143),
    // oldest first, ending today in the reader's time zone, each day rounded to whole
    // minutes. PLUGIN 172-179 reads startDate with a yyyy-mm-dd match and every entry
    // with tonumber(v) or 0; a missing startDate or minutes leaves the year empty.
    {
        const QJsonValue year = body.value(QStringLiteral("readingYear"));
        if (isObject(year)) {
            const QJsonObject o = year.toObject();
            const QDate start = parseDay(o.value(QStringLiteral("startDate")));
            const QJsonValue minutes = o.value(QStringLiteral("minutes"));
            if (start.isValid() && minutes.isArray()) {
                const QJsonArray a = minutes.toArray();
                d.readingYear.startDate = start;
                d.readingYear.minutes.reserve(a.size());
                for (int i = 0; i < a.size(); ++i)
                    d.readingYear.minutes.append(roundedMin0(a.at(i)));
                d.hasReadingYear = true;
            }
        }
    }

    // readingStreak { currentStreak, longestStreak, lastSevenDays }: WIDGET 152-156.
    // PLUGIN 698-703 reads currentStreak (through CatalogUtil.readingStreakDays,
    // bookorbit_catalog_util.lua 228-233), longestStreak and lastSevenDays, each on its
    // own, defaulting to 0 and {}. lastSevenDays is oldest first and ends today (SERVER
    // computeStreakData, dashboard-widget.calculations.ts 65-69).
    {
        const QJsonValue streak = body.value(QStringLiteral("readingStreak"));
        if (isObject(streak)) {
            const QJsonObject o = streak.toObject();
            DashboardStreak s;
            if (isNumber(o.value(QStringLiteral("currentStreak")))) {
                s.hasCurrent = true;
                s.current = roundedMin0(o.value(QStringLiteral("currentStreak")));
            }
            if (isNumber(o.value(QStringLiteral("longestStreak")))) {
                s.hasLongest = true;
                s.longest = roundedMin0(o.value(QStringLiteral("longestStreak")));
            }
            const QJsonValue seven = o.value(QStringLiteral("lastSevenDays"));
            if (seven.isArray()) {
                const QJsonArray a = seven.toArray();
                s.hasLastSeven = true;
                for (int i = 0; i < a.size(); ++i)
                    s.lastSevenDays.append(a.at(i).toBool(false));
            }
            d.readingStreak = s;
            d.hasReadingStreak = s.hasCurrent || s.hasLongest || s.hasLastSeven;
        }
    }

    // readingGoal { goalBooks: number | null, completedBooks, year }: WIDGET 120-124.
    // SERVER getReadingGoal (dashboard-widget.service.ts 126-142): goalBooks is null until
    // the reader sets one. PLUGIN 690-695: books = tonumber(goalBooks) (nil when null),
    // done = tonumber(completedBooks) or 0, year = tonumber(year).
    {
        const QJsonValue goal = body.value(QStringLiteral("readingGoal"));
        if (isObject(goal)) {
            const QJsonObject o = goal.toObject();
            DashboardGoal g;
            if (isNumber(o.value(QStringLiteral("goalBooks")))) {
                g.hasGoalBooks = true;
                g.goalBooks = roundedMin0(o.value(QStringLiteral("goalBooks")));
            }
            if (isNumber(o.value(QStringLiteral("completedBooks")))) {
                g.hasCompleted = true;
                g.completedBooks = roundedMin0(o.value(QStringLiteral("completedBooks")));
            }
            if (isNumber(o.value(QStringLiteral("year")))) {
                g.hasYear = true;
                g.year = int(o.value(QStringLiteral("year")).toDouble());
            }
            d.readingGoal = g;
            d.hasReadingGoal = g.hasGoalBooks || g.hasCompleted;
        }
    }

    // finishedThisYear: KoreaderCatalogBookListItem[] with finishedOn, most recently
    // finished first (TYPES 339-342, 541; SERVER buildFinishedThisYear, limit 12 at
    // koreader-catalog.service.ts 145). PLUGIN 674 shows them as a shelf.
    {
        const QJsonValue finished = body.value(QStringLiteral("finishedThisYear"));
        if (finished.isArray()) {
            const QJsonArray a = finished.toArray();
            for (int i = 0; i < a.size(); ++i)
                if (a.at(i).isObject())
                    d.finishedThisYear.append(parseBook(a.at(i).toObject()));
            d.hasFinishedThisYear = true;
        }
    }

    // yearAgo { book, minutes, days, endProgress, firstDay, lastDay } or null: TYPES 488-495.
    // PLUGIN 729-738: needs a book table; minutes and days default to 0; stopped_at is
    // endProgress rounded; `when` comes from firstDay.
    {
        const QJsonValue ago = body.value(QStringLiteral("yearAgo"));
        if (isObject(ago)) {
            const QJsonObject o = ago.toObject();
            if (isObject(o.value(QStringLiteral("book")))) {
                DashboardYearAgo y;
                y.book = parseBook(o.value(QStringLiteral("book")).toObject());
                if (isNumber(o.value(QStringLiteral("minutes")))) {
                    y.hasMinutes = true;
                    y.minutes = roundedMin0(o.value(QStringLiteral("minutes")));
                }
                if (isNumber(o.value(QStringLiteral("days")))) {
                    y.hasDays = true;
                    y.days = roundedMin0(o.value(QStringLiteral("days")));
                }
                if (isNumber(o.value(QStringLiteral("endProgress")))) {
                    y.hasEndProgress = true;
                    y.endProgress = o.value(QStringLiteral("endProgress")).toDouble();
                }
                y.firstDay = parseDay(o.value(QStringLiteral("firstDay")));
                // A card with no title has nothing to say.
                if (!y.book.title.isEmpty()) {
                    d.yearAgo = y;
                    d.hasYearAgo = true;
                }
            }
        }
    }

    // achievements { earned, total, next, latest } or null (reader turned them off):
    // TYPES 514-519, 543; SERVER buildAchievementSummary (koreader-catalog.service.ts
    // 300-330). PLUGIN 508-523: missing or non-table means no widget; badge() drops a
    // badge with no string name. Smalt shows only `next` (the "Next badge" row).
    {
        const QJsonValue ach = body.value(QStringLiteral("achievements"));
        if (isObject(ach)) {
            const QJsonValue next = ach.toObject().value(QStringLiteral("next"));
            if (isObject(next)) {
                bool ok = false;
                const DashboardBadge b = parseBadge(next.toObject(), &ok);
                if (ok) {
                    d.nextBadge = b;
                    d.hasNextBadge = true;
                }
            }
        }
    }

    // highlightOfTheDay { text, note, bookTitle, bookId, ... } or null: WIDGET 166-174.
    // PLUGIN 704: used only when it is a table. Needs some text to show.
    {
        const QJsonValue hl = body.value(QStringLiteral("highlightOfTheDay"));
        if (isObject(hl)) {
            const QJsonObject o = hl.toObject();
            DashboardHighlight h;
            h.text = stringOf(o.value(QStringLiteral("text"))).trimmed();
            h.note = stringOf(o.value(QStringLiteral("note")));
            h.bookTitle = stringOf(o.value(QStringLiteral("bookTitle")));
            h.bookId = qint64(o.value(QStringLiteral("bookId")).toDouble());
            if (!h.text.isEmpty()) {
                d.highlight = h;
                d.hasHighlight = true;
            }
        }
    }

    return d;
}

QJsonObject trimDashboard(const QJsonObject &body)
{
    static const char *const keys[] = { "generatedAt",  "readingYear",      "readingStreak",
                                        "readingGoal",  "finishedThisYear", "yearAgo",
                                        "achievements", "highlightOfTheDay" };
    QJsonObject out;
    for (const char *k : keys) {
        const QString key = QString::fromLatin1(k);
        if (body.contains(key))
            out.insert(key, body.value(key));
    }
    return out;
}

Status cacheDashboard(Ledger *ledger, const QJsonObject &body, const QDateTime &nowUtc)
{
    if (!ledger)
        return Status::error(StatusCode::InvalidArgument, QStringLiteral("no ledger"));
    const QByteArray bytes = QJsonDocument(trimDashboard(body)).toJson(QJsonDocument::Compact);
    return ledger->cachePut(QString::fromLatin1(kDashboardCacheKey),
                            QString::fromLatin1(kDashboardCacheKind), bytes, nowUtc);
}

Status loadCachedDashboard(Ledger *ledger, Dashboard *out, QDateTime *updatedUtc)
{
    if (!ledger || !out)
        return Status::error(StatusCode::InvalidArgument, QStringLiteral("no ledger"));
    QByteArray bytes;
    QDateTime updated;
    const Status s = ledger->cacheGet(QString::fromLatin1(kDashboardCacheKey), &bytes, &updated);
    if (!s.ok())
        return s;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject())
        return Status::error(StatusCode::NotFound, QStringLiteral("cached dashboard unreadable"));
    *out = parseDashboard(doc.object());
    if (updatedUtc)
        *updatedUtc = updated;
    return Status::success();
}

} // namespace smalt
