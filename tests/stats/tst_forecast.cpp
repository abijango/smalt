// Task 23, acceptance test: the finish forecast. SPEC: Stats ("Rules"): the
// median minutes per reading day over the last 14 days and pages per hour over
// the last 30; with fewer than three sessions "Not enough reading yet".
#include "test_support.h"

#include "stats/aggregate.h"
#include "stats/format.h"

#include <QtTest>

using namespace smalt;
using namespace smalt::stats;
using namespace statstest;

namespace {

Pace pace(double pph)
{
    Pace p;
    p.valid = true;
    p.pagesPerHour = pph;
    p.pages = 40;
    p.seconds = 3600;
    return p;
}

// Fourteen days, today last; `minutes` fills the end, the rest are zero.
QVector<qint64> days(const QList<int> &minutes)
{
    QVector<qint64> out(14, 0);
    for (int i = 0; i < minutes.size(); ++i)
        out[14 - minutes.size() + i] = qint64(minutes.at(i)) * 60;
    return out;
}

ForecastInput input()
{
    ForecastInput in;
    in.percent = 41;
    in.totalPages = 300;
    in.sessionCount = 5;
    in.bookPace = pace(40);
    in.recentDaySeconds = days(QList<int>() << 20 << 0 << 30 << 40 << 0 << 30);
    in.today = QDate(2026, 11, 20); // A Friday.
    return in;
}

} // namespace

class TstForecast : public QObject
{
    Q_OBJECT

private slots:

    void matchesTheFormulaWithMedianMinutesAndPagesPerHour()
    {
        const Forecast f = forecast(input());
        QCOMPARE(f.state, Forecast::Ok);
        // 300 pages, 41% read: 177 pages left at 40 pages an hour = 265.5 minutes.
        QVERIFY(qAbs(f.minutesLeft - 265.5) < 1e-9);
        // Reading days 20, 30, 30, 40: the median is 30 (zero days are not reading days).
        QCOMPARE(f.perDayMinutes, 30);
        // 265.5 / 30 = 8.85: nine reading days, the first one today: done on day 8 from now.
        QCOMPARE(f.daysOut, 8);
        QCOMPARE(f.finishDate, QDate(2026, 11, 28));
        QCOMPARE(forecastHeadline(f), QStringLiteral("About 4 h 25 m left"));
        QCOMPARE(forecastDetail(f, input().today),
                 QStringLiteral("Done around 28 Nov at your pace of 30 min a day"));
    }

    void anEvenNumberOfReadingDaysTakesTheMiddleTwo()
    {
        ForecastInput in = input();
        in.recentDaySeconds = days(QList<int>() << 20 << 40);
        QCOMPARE(forecast(in).perDayMinutes, 30);
        in.recentDaySeconds = days(QList<int>() << 10 << 20 << 50 << 80);
        QCOMPARE(forecast(in).perDayMinutes, 35);
    }

    void closeFinishesNameTheDay()
    {
        ForecastInput in = input();
        in.percent = 90; // 30 pages left = 45 minutes.
        Forecast f = forecast(in);
        QCOMPARE(f.daysOut, 1);
        QCOMPARE(forecastDetail(f, in.today),
                 QStringLiteral("Done around tomorrow at your pace of 30 min a day"));
        in.percent = 99; // Under a day's reading.
        f = forecast(in);
        QCOMPARE(f.daysOut, 0);
        QCOMPARE(forecastDetail(f, in.today),
                 QStringLiteral("Done around today at your pace of 30 min a day"));
        QCOMPARE(forecastHeadline(f), QStringLiteral("About 5 min left"));
        in.percent = 66; // 102 pages = 153 minutes: six reading days, done on the fifth day out.
        f = forecast(in);
        QCOMPARE(f.daysOut, 5);
        QCOMPARE(forecastDetail(f, in.today),
                 QStringLiteral("Done around Wednesday at your pace of 30 min a day"));
    }

    void fewerThanThreeSessionsIsNotEnoughReading()
    {
        ForecastInput in = input();
        in.sessionCount = 2;
        const Forecast f = forecast(in);
        QCOMPARE(f.state, Forecast::NotEnoughReading);
        QCOMPARE(forecastHeadline(f), QStringLiteral("Not enough reading yet"));
        QVERIFY(forecastDetail(f, in.today).isEmpty());
        in.sessionCount = 3; // Three is enough.
        QCOMPARE(forecast(in).state, Forecast::Ok);
    }

    void fallsBackToTheDevicesPaceWhenTheBookHasNoneInTheLast30Days()
    {
        ForecastInput in = input();
        in.bookPace = Pace();
        in.devicePace = pace(20);
        const Forecast f = forecast(in);
        QCOMPARE(f.state, Forecast::Ok);
        QVERIFY(qAbs(f.minutesLeft - 531.0) < 1e-9); // 177 pages at 20 an hour.
        in.devicePace = Pace();
        QCOMPARE(forecast(in).state, Forecast::NotEnoughReading);
    }

    void noReadingDaysInTheLast14DaysIsNotEnoughReading()
    {
        ForecastInput in = input();
        in.recentDaySeconds = QVector<qint64>(14, 0);
        QCOMPARE(forecast(in).state, Forecast::NotEnoughReading);
    }

    void aFinishedBookHasNoForecast()
    {
        ForecastInput in = input();
        in.percent = 100;
        QCOMPARE(forecast(in).state, Forecast::Finished);
    }

    void unknownPageCountIsNotEnoughReading()
    {
        ForecastInput in = input();
        in.totalPages = 0;
        QCOMPARE(forecast(in).state, Forecast::NotEnoughReading);
    }

    void recentDaysListsTodayLast()
    {
        QMap<QDate, qint64> perDay;
        perDay[QDate(2026, 11, 20)] = 60;
        perDay[QDate(2026, 11, 7)] = 120;
        perDay[QDate(2026, 11, 6)] = 999; // 14 days back: outside.
        const QVector<qint64> r = recentDays(perDay, QDate(2026, 11, 20), 14);
        QCOMPARE(r.size(), 14);
        QCOMPARE(r.last(), qint64(60));
        QCOMPARE(r.first(), qint64(120));
    }
};

QTEST_MAIN(TstForecast)
#include "tst_forecast.moc"
