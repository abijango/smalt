#include "snapshot.h"

#include "smalt/ledger.h"

#include <algorithm>

namespace smalt {
namespace stats {

namespace {

qint64 secsOf(const QDateTime &t)
{
    return t.isValid() ? t.toMSecsSinceEpoch() / 1000 : 0;
}

int minutesOf(qint64 seconds)
{
    if (seconds <= 0)
        return 0;
    return std::max<int>(1, int((seconds + 30) / 60));
}

int daysInYearOf(int year)
{
    return QDate(year, 12, 31).dayOfYear();
}

} // namespace

StatsSnapshot buildSnapshot(const StatsInputs &in)
{
    StatsSnapshot s;
    const qint64 nowSecs = secsOf(in.nowUtc);
    const ZoneOffsetFn zone = in.zone ? in.zone : systemZone();
    s.today = localDate(nowSecs, zone);
    s.heatmapWeeks = in.heatmapWeeks;
    const Dashboard &d = in.dashboard;
    const bool serverYear = in.hasDashboard && d.hasReadingYear;

    // --- Merge this Kobo's unsent minutes into the server's heatmap ---
    const qint64 fetched = serverYear ? std::max<qint64>(0, secsOf(in.dashboardFetchedUtc)) : -1;
    const SessionRecord *open = in.hasOpenSession ? &in.openSession : nullptr;
    const QMap<QDate, qint64> unsynced =
            unsyncedSecondsPerDay(in.events, in.confirmed, fetched, open, zone);
    const QMap<QDate, int> merged = mergeMinutes(serverYear ? &d.readingYear : nullptr, unsynced);

    s.showHeatmap = serverYear;
    if (s.showHeatmap)
        s.heatmap = buildHeatmap(merged, s.today, in.heatmapWeeks);

    s.tilesFromThisKoboOnly = !serverYear;
    s.todayMinutes = merged.value(s.today, 0);
    for (int i = 0; i < 7; ++i)
        s.weekMinutes += merged.value(s.today.addDays(-i), 0);
    // Without a server year and without local reading there is nothing to tile.
    s.showTiles = serverYear || !merged.isEmpty();

    // --- Streak ---
    if (in.hasDashboard && d.hasReadingStreak) {
        s.showStreak = true;
        s.hasStreakCurrent = d.readingStreak.hasCurrent;
        s.streakCurrent = adjustedStreak(d.readingStreak.current, s.todayMinutes);
        s.hasStreakLongest = d.readingStreak.hasLongest;
        s.streakLongest = d.readingStreak.longest;
        const bool seven =
                d.readingStreak.hasLastSeven && d.readingStreak.lastSevenDays.size() == 7;
        for (int i = 0; i < 7; ++i) {
            const QDate day = s.today.addDays(i - 6);
            const bool read =
                    merged.value(day, 0) > 0 || (seven && d.readingStreak.lastSevenDays.at(i));
            s.streakDots.append(read);
            s.streakInitials.append(weekdayInitial(day.dayOfWeek()));
        }
    }

    // --- Goal ---
    if (in.hasDashboard && d.hasReadingGoal && d.readingGoal.hasGoalBooks
        && d.readingGoal.goalBooks > 0) {
        const DashboardGoal &g = d.readingGoal;
        const int completed = g.hasCompleted ? g.completedBooks : 0;
        const int year = g.hasYear ? g.year : s.today.year();
        int dayOfYear = s.today.dayOfYear();
        if (year < s.today.year())
            dayOfYear = daysInYearOf(year);
        else if (year > s.today.year())
            dayOfYear = 0;
        s.showGoal = true;
        s.goalTitle = goalTitle(g, completed);
        s.goalPace = goalPace(g, completed, dayOfYear, daysInYearOf(year));
        s.goalFraction = goalFraction(g, completed);
    }

    // --- Today's blocks, with the open session's page on screen ---
    {
        QVector<PageEventRecord> withOpen = in.events;
        if (open && open->state == SessionState::Open && open->endUtc.isValid()) {
            qint64 inEvents = 0;
            for (const PageEventRecord &e : in.events)
                if (e.sessionUuid == open->uuid)
                    inEvents += std::min(std::max(e.durationSeconds, 0), kMaxPageSeconds);
            const qint64 extra = open->activeSeconds - inEvents;
            if (extra > 0) {
                PageEventRecord e;
                e.sessionUuid = open->uuid;
                e.contentId = open->contentId;
                e.startUtc = QDateTime::fromMSecsSinceEpoch((secsOf(open->endUtc) - extra) * 1000,
                                                            Qt::UTC);
                e.durationSeconds = int(std::min<qint64>(extra, kMaxPageSeconds));
                withOpen.append(e);
            }
        }
        s.todayBlocks = blocksForDay(withOpen, s.today, zone);
    }

    // --- Patterns (this Kobo) ---
    const QMap<QDate, qint64> perDay = secondsPerDay(in.events, zone);
    s.hasReading = !in.events.isEmpty() || !in.sessions.isEmpty();
    const QDate patternFrom = s.today.addDays(-(kPatternDays - 1));
    s.secondsByHour = secondsByHour(in.events, zone, patternFrom, s.today);
    s.hourSummary = hourSummary(s.secondsByHour);
    s.weekdayAverages = averageMinutesByWeekday(perDay, patternFrom, s.today);
    s.records = computeRecords(in.sessions, perDay, zone);
    s.pace30 = pagesPerHour(in.events, nowSecs - qint64(kPaceDays) * 86400, nowSecs + 86400);

    // --- Year (all devices) ---
    s.finishedYear = s.today.year();
    if (in.hasDashboard
        && (d.hasFinishedThisYear || (d.hasReadingGoal && d.readingGoal.hasCompleted))) {
        s.showFinished = true;
        s.finishedBooks = d.finishedThisYear;
        s.finishedCount = (d.hasReadingGoal && d.readingGoal.hasCompleted)
                ? d.readingGoal.completedBooks
                : d.finishedThisYear.size();
        s.finishedCount = std::max(s.finishedCount, int(d.finishedThisYear.size()));
    }
    if (serverYear) {
        s.hasYearSeconds = true;
        for (QMap<QDate, int>::const_iterator it = merged.constBegin(); it != merged.constEnd();
             ++it)
            if (it.key().year() == s.today.year())
                s.yearSeconds += qint64(it.value()) * 60;
    }
    if (in.hasDashboard && d.hasYearAgo) {
        s.showYearAgo = true;
        s.yearAgo = d.yearAgo;
    }
    if (in.hasDashboard && d.hasNextBadge) {
        s.showBadge = true;
        s.badge = d.nextBadge;
    }
    if (in.hasDashboard && d.hasHighlight) {
        s.showHighlight = true;
        s.highlight = d.highlight;
    }
    return s;
}

StatsInputs loadInputs(Ledger *ledger, const DeviceProfile &profile, const QDateTime &nowUtc,
                       const ZoneOffsetFn &zone)
{
    StatsInputs in;
    in.nowUtc = nowUtc;
    in.zone = zone;
    in.heatmapWeeks = profile.heatmapWeeks;
    if (!ledger || !ledger->isOpen())
        return in;

    in.hasDashboard = loadCachedDashboard(ledger, &in.dashboard, &in.dashboardFetchedUtc).ok();

    const QDateTime from = nowUtc.addDays(-kHistoryDays);
    const QDateTime to = nowUtc.addDays(1);
    in.events = ledger->pageEventsInRange(from, to, &in.confirmed);
    in.sessions = ledger->sessionsInRange(QDateTime::fromMSecsSinceEpoch(0, Qt::UTC), to);
    SessionRecord open;
    if (ledger->openSession(&open).ok()) {
        in.hasOpenSession = true;
        in.openSession = open;
    }
    return in;
}

BookSnapshot buildBookSnapshot(const BookInputs &in)
{
    BookSnapshot b;
    const ZoneOffsetFn zone = in.zone ? in.zone : systemZone();
    const qint64 nowSecs = secsOf(in.nowUtc);
    b.today = localDate(nowSecs, zone);
    b.stats = computeBookStats(in.sessions, in.bookEvents, zone);
    b.hasReading = b.stats.sessionCount > 0 || !in.bookEvents.isEmpty();
    for (QMap<QDate, qint64>::const_iterator it = b.stats.perDay.constBegin();
         it != b.stats.perDay.constEnd(); ++it)
        b.minutesPerDay.insert(it.key(), minutesOf(it.value()));

    for (const SessionRecord &s : in.latest) {
        if (s.state == SessionState::Dropped)
            continue;
        SessionRow row;
        const qint64 start = secsOf(s.startUtc);
        row.day = formatDayLabel(localDate(start, zone), b.today);
        row.range = formatRange(secondsIntoLocalDay(start, zone),
                                s.endUtc.isValid() ? secondsIntoLocalDay(secsOf(s.endUtc), zone)
                                                   : secondsIntoLocalDay(start, zone));
        row.duration = formatMinutes(s.activeSeconds);
        b.latest.append(row);
        if (b.latest.size() >= kLatestSessions)
            break;
    }

    ForecastInput f;
    f.percent = in.hasPercent ? in.percent : 0;
    f.totalPages = b.stats.totalPages;
    f.sessionCount = b.stats.sessionCount;
    const qint64 from = nowSecs - qint64(kPaceDays) * 86400;
    f.bookPace = pagesPerHour(in.bookEvents, from, nowSecs + 86400);
    f.devicePace = pagesPerHour(in.deviceEvents, from, nowSecs + 86400);
    f.recentDaySeconds = recentDays(secondsPerDay(in.deviceEvents, zone), b.today, kForecastDays);
    f.today = b.today;
    b.forecast = in.hasPercent ? forecast(f) : Forecast();
    return b;
}

BookInputs loadBookInputs(Ledger *ledger, const QString &contentId, const QDateTime &nowUtc,
                          const ZoneOffsetFn &zone)
{
    BookInputs in;
    in.nowUtc = nowUtc;
    in.zone = zone;
    if (!ledger || !ledger->isOpen() || contentId.isEmpty())
        return in;

    in.sessions = ledger->sessionsForBook(contentId);
    in.latest = ledger->recentSessionsForBook(contentId, kLatestSessions);
    in.bookEvents = ledger->pageEventsAfter(contentId, 0, 0);
    in.deviceEvents =
            ledger->pageEventsInRange(nowUtc.addDays(-(kPaceDays + 1)), nowUtc.addDays(1));

    ProgressRecord p;
    if (ledger->progress(contentId, &p).ok()) {
        in.hasPercent = true;
        in.percent = p.local.percent;
    } else if (!in.latest.isEmpty()) {
        // No progress row yet: where the newest session ended.
        in.hasPercent = true;
        in.percent = in.latest.first().end.percent;
    }
    return in;
}

} // namespace stats
} // namespace smalt
