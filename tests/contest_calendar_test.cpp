// contest_calendar_test — the contest calendar's recurrence maths (#4 item 3).
//
// The whole design rests on computing dates from rules rather than shipping a
// date table, so the maths is the thing that has to be right. Every expected
// date below was computed INDEPENDENTLY (Python's calendar module) rather
// than read off what this code produces — a test that asserts whatever the
// implementation happens to return proves only that it is deterministic.
//
// The cases that would be easy to get wrong, and are pinned here:
//   - the rule must give a DIFFERENT date each year, which is the entire
//     reason for not shipping a date table
//   - a 5th Saturday does not exist in every month
//   - an event running across New Year must still be found on 1 January
//   - "upcoming" asked in December must look into next year

#include "ContestCalendar.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cstdio>

using namespace ShackBook;

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { ++g_failures; std::printf("FAIL: %s\n", what); }
    else       std::printf("[ OK ] %s\n", what);
}

static void checkDate(const QDate& got, const QDate& want, const char* what)
{
    const bool ok = (got == want);
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s (got %s, want %s)\n", what,
                    qPrintable(got.toString(Qt::ISODate)),
                    qPrintable(want.toString(Qt::ISODate)));
    } else {
        std::printf("[ OK ] %s = %s\n", what, qPrintable(got.toString(Qt::ISODate)));
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    ContestCalendar cal;
    const int n = cal.load(QStringLiteral(SHACKBOOK_CALENDAR_DAT));
    std::printf("loaded %d calendar events\n", n);
    check(n > 0, "the bundled calendar loads");
    if (n == 0) { std::fprintf(stderr, "cannot continue\n"); return 1; }

    // ── ⭐ The rule yields a DIFFERENT date each year ───────────────────
    // This is the point of the whole design: a bundled date table would be
    // stale the following year, on the machine least able to refresh it.
    {
        ContestEvent e;
        e.name = QStringLiteral("2nd weekend of August");
        e.rule = Recurrence::NthWeekendOfMonth;
        e.month = 8; e.nth = 2; e.durationDays = 2;
        checkDate(e.startIn(2026), QDate(2026, 8, 8),  "2nd Sat of Aug 2026");
        checkDate(e.startIn(2027), QDate(2027, 8, 14), "2nd Sat of Aug 2027");
        check(e.startIn(2026) != e.startIn(2027),
              "the SAME rule gives different dates in different years");
        checkDate(e.endIn(2026), QDate(2026, 8, 9),
                  "a 2-day weekend ends the day after it starts");
    }
    {
        ContestEvent e;
        e.name = QStringLiteral("last weekend of August");
        e.rule = Recurrence::LastWeekendOfMonth;
        e.month = 8; e.durationDays = 2;
        checkDate(e.startIn(2026), QDate(2026, 8, 29), "last Sat of Aug 2026");
        checkDate(e.startIn(2027), QDate(2027, 8, 28), "last Sat of Aug 2027");
    }
    {
        // ⭐ A 5th Saturday does not exist in every month. February 2026 has
        // four; returning a date anyway would silently invent an event.
        ContestEvent e;
        e.name = QStringLiteral("5th weekend");
        e.rule = Recurrence::NthWeekendOfMonth;
        e.durationDays = 2; e.nth = 5;
        e.month = 2;
        check(!e.startIn(2026).isValid(), "a 5th Saturday of Feb 2026 is INVALID, not invented");
        e.month = 8;
        checkDate(e.startIn(2026), QDate(2026, 8, 29), "but Aug 2026 really has one");
    }
    {
        ContestEvent e;
        e.name = QStringLiteral("single day");
        e.rule = Recurrence::NthSaturdayOfMonth;
        e.month = 10; e.nth = 1; e.durationDays = 1;
        checkDate(e.startIn(2026), QDate(2026, 10, 3), "1st Sat of Oct 2026");
        checkDate(e.endIn(2026),   QDate(2026, 10, 3), "a 1-day event ends the day it starts");
    }

    // ── The bundled rows resolve to the dates computed independently ────
    {
        const auto up = cal.upcoming(QDate(2026, 1, 1), 20);
        auto findStart = [&](const char* id) {
            for (const auto& o : up)
                if (o.event.contestId == QLatin1String(id)) return o.start;
            return QDate{};
        };
        checkDate(findStart("MDC-QSO-PARTY"), QDate(2026, 8, 8),  "MDC-QSO-PARTY 2026");
        checkDate(findStart("CQP"),           QDate(2026, 10, 3), "CQP 2026");
        checkDate(findStart("PAQP"),          QDate(2026, 10, 10),"PAQP 2026");
        checkDate(findStart("TXQP"),          QDate(2026, 9, 26), "TXQP 2026");
        checkDate(findStart("FLQP"),          QDate(2026, 4, 25), "FLQP 2026");
    }

    // ── runningOn ──────────────────────────────────────────────────────
    {
        const auto on = cal.runningOn(QDate(2026, 8, 8));
        check(!on.isEmpty(), "the MDC party is found on its own Saturday");
        bool sawMdc = false;
        for (const auto& o : on) if (o.event.contestId == "MDC-QSO-PARTY") sawMdc = true;
        check(sawMdc, "and it is the right event");

        check(!cal.runningOn(QDate(2026, 8, 9)).isEmpty(),
              "still running on the Sunday of a 2-day weekend");
        check(cal.runningOn(QDate(2026, 8, 10)).isEmpty(),
              "but NOT on the Monday after");
        check(cal.runningOn(QDate(2026, 6, 15)).isEmpty(),
              "a quiet date has nothing running");
        check(cal.runningOn(QDate()).isEmpty(), "an invalid date yields nothing");
    }

    // ── ⭐ An event spanning New Year is found on 1 January ─────────────
    // Its start date lies in the PREVIOUS year, so a naive same-year search
    // would miss it entirely.
    {
        const QString tmp = QDir::tempPath() + QStringLiteral("/shackbook_cal_ny_test.dat");
        QFile f(tmp);
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
              "can write a temporary calendar");
        {
            QTextStream out(&f);
            // 31 December, running 3 days -> 31 Dec, 1 Jan, 2 Jan.
            out << "NYE|New Year Event|FIXED|12|0|3|31" << Qt::endl;
        }
        f.close();

        ContestCalendar c2;
        check(c2.load(tmp) == 1, "the temporary calendar loads");
        check(!c2.runningOn(QDate(2026, 12, 31)).isEmpty(), "running on 31 Dec 2026");
        check(!c2.runningOn(QDate(2027, 1, 1)).isEmpty(),
              "STILL running on 1 Jan 2027 — the start date is in the previous year");
        check(c2.runningOn(QDate(2027, 1, 3)).isEmpty(), "finished by 3 Jan");
        QFile::remove(tmp);
    }

    // ── upcoming ───────────────────────────────────────────────────────
    {
        const auto up = cal.upcoming(QDate(2026, 1, 1), 3);
        check(up.size() == 3, "upcoming respects its limit");
        for (int i = 1; i < up.size(); ++i)
            if (up[i].start < up[i - 1].start) {
                check(false, "upcoming is sorted soonest-first");
                break;
            }
        check(up.first().start >= QDate(2026, 1, 1), "and never returns a past event");

        // ⭐ Asked in December, "nothing coming up" would be wrong rather than
        // merely unhelpful — next year's events must be reachable.
        const auto dec = cal.upcoming(QDate(2026, 12, 20), 3);
        check(!dec.isEmpty(), "asking in December finds NEXT year's events");
        if (!dec.isEmpty())
            check(dec.first().start.year() == 2027, "and they are in 2027");

        check(cal.upcoming(QDate(2026, 1, 1), 0).isEmpty(), "a zero limit yields nothing");
        check(cal.upcoming(QDate(), 3).isEmpty(), "an invalid date yields nothing");
    }

    // ── A malformed rule rejects its row rather than guessing ───────────
    {
        const QString tmp = QDir::tempPath() + QStringLiteral("/shackbook_cal_bad_test.dat");
        QFile f(tmp);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        {
            QTextStream out(&f);
            out << "GOOD|Good|NTH_WEEKEND|5|1|2|0"        << Qt::endl;
            out << "BADRULE|Bad|SOMETIME_IN_MAY|5|1|2|0"  << Qt::endl;
            out << "BADMONTH|Bad Month|NTH_WEEKEND|13|1|2|0" << Qt::endl;
            out << "SHORT|Too Few Columns|NTH_WEEKEND|5|1" << Qt::endl;
        }
        f.close();
        ContestCalendar c3;
        check(c3.load(tmp) == 1, "only the well-formed row loads");
        QFile::remove(tmp);
    }

    bool ok = false;
    check(ContestCalendar::ruleFromName("LAST_WEEKEND", &ok) == Recurrence::LastWeekendOfMonth && ok,
          "a rule name parses");
    check(ContestCalendar::ruleName(Recurrence::LastWeekendOfMonth) == "LAST_WEEKEND",
          "and renders back to the same name");

    if (g_failures == 0) {
        std::printf("contest_calendar_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "contest_calendar_test: %d failure(s)\n", g_failures);
    return 1;
}
