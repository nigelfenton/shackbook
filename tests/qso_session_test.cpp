// qso_session_test — the derived session boundary (#3).
//
// A session is not in the schema; it is derived from a gap of MORE THAN two
// hours between consecutive contacts. The cases below pin the decisions
// recorded on the issue, and in particular the ones that would be easy to
// get subtly wrong:
//
//   - the boundary is STRICT: exactly 2 h stays in one session, 2 h + 1 s splits
//   - an evening crossing midnight UTC stays in ONE session (the whole reason
//     a plain date filter was rejected)
//   - unsorted input still sessions correctly
//   - a QSO whose time cannot be parsed is EXCLUDED and counted, never folded
//     into a neighbouring session

#include "QsoSession.h"

#include <QCoreApplication>
#include <cstdio>

using namespace ShackBook;

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { ++g_failures; std::printf("FAIL: %s\n", what); }
    else       std::printf("[ OK ] %s\n", what);
}

static Qso mk(const char* date, const char* time, const char* call = "W1AW")
{
    Qso q;
    q.qsoDate = QString::fromLatin1(date);
    q.timeOn  = QString::fromLatin1(time);
    q.call    = QString::fromLatin1(call);
    return q;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ── Time parsing ───────────────────────────────────────────────────
    check(qsoDateTime(mk("20260827", "1930")).isValid(), "HHMM time parses");
    check(qsoDateTime(mk("20260827", "193045")).isValid(), "HHMMSS time parses");
    check(!qsoDateTime(mk("20260827", "")).isValid(),      "empty time is invalid");
    check(!qsoDateTime(mk("", "1930")).isValid(),          "empty date is invalid");
    check(!qsoDateTime(mk("2026082", "1930")).isValid(),   "short date is invalid");
    check(!qsoDateTime(mk("20260230", "1930")).isValid(),  "30 February is invalid");
    check(!qsoDateTime(mk("20260827", "2565")).isValid(),  "25:65 is invalid");
    // Assert the OFFSET, not the timeSpec enum: constructing with
    // QTimeZone::utc() yields Qt::TimeZone, not Qt::UTC, so a timeSpec check
    // fails while the time is in fact correct UTC. What actually matters for
    // the gap arithmetic is that the instant is right.
    check(qsoDateTime(mk("20260827", "1930")).offsetFromUtc() == 0,
          "ADIF times are anchored to UTC (zero offset)");
    check(qsoDateTime(mk("20260827", "1930")).toUTC().time() == QTime(19, 30),
          "and the UTC clock time is the logged time, not shifted by the local zone");

    // ── ⭐ The boundary is STRICT at 2 hours ────────────────────────────
    {
        // 19:00 then 21:00 — exactly two hours.
        QVector<Qso> v{mk("20260827", "1900"), mk("20260827", "2100")};
        check(splitSessions(v).size() == 1,
              "a gap of EXACTLY 2 h stays in one session");
    }
    {
        // 19:00 then 21:00:01 — one second over.
        QVector<Qso> v{mk("20260827", "190000"), mk("20260827", "210001")};
        check(splitSessions(v).size() == 2,
              "a gap of 2 h + 1 s starts a new session");
    }

    // ── ⭐ An evening crossing midnight UTC stays in one piece ──────────
    {
        // 22:30 and 00:30 the next UTC day, two hours apart.
        QVector<Qso> v{mk("20260827", "2230"), mk("20260828", "0030")};
        const auto s = splitSessions(v);
        check(s.size() == 1, "an evening crossing midnight UTC is ONE session");
        check(s.first().count() == 2, "and holds both QSOs");
    }

    // ── An afternoon and an evening DO split ───────────────────────────
    {
        QVector<Qso> v{mk("20260827", "1400"), mk("20260827", "1430"),
                       mk("20260827", "1930"), mk("20260827", "2000")};
        const auto s = splitSessions(v);
        check(s.size() == 2, "an afternoon and an evening are two sessions");
        check(s.first().count() == 2 && s.last().count() == 2,
              "each session keeps its own QSOs");
    }

    // ── Newest first, because "tonight" is what the map wants ──────────
    {
        QVector<Qso> v{mk("20260825", "1400"), mk("20260827", "2000")};
        const auto s = splitSessions(v);
        check(s.size() == 2, "two days apart are two sessions");
        check(s.first().start.date() == QDate(2026, 8, 27),
              "session 0 is the MOST RECENT");
    }

    // ── Unsorted input must still session correctly ────────────────────
    {
        QVector<Qso> v{mk("20260827", "2000"), mk("20260827", "1400"),
                       mk("20260827", "1930"), mk("20260827", "1430")};
        const auto s = splitSessions(v);
        check(s.size() == 2, "unsorted input still yields two sessions");
        check(s.last().start.time() == QTime(14, 0),
              "the oldest session starts at the earliest contact");
    }

    // ── ⭐ Unparseable QSOs are excluded AND counted ────────────────────
    {
        QVector<Qso> v{mk("20260827", "1900"), mk("", ""), mk("20260827", "1930")};
        int bad = -1;
        const auto s = splitSessions(v, 2, &bad);
        check(bad == 1, "an unplaceable QSO is counted");
        check(s.size() == 1 && s.first().count() == 2,
              "and excluded from the session rather than folded in");
    }

    // ── Degenerate inputs ──────────────────────────────────────────────
    check(splitSessions({}).isEmpty(), "no QSOs yields no sessions");
    {
        QVector<Qso> v{mk("", "")};
        int bad = 0;
        check(splitSessions(v, 2, &bad).isEmpty() && bad == 1,
              "only-unplaceable input yields no sessions");
    }
    {
        QVector<Qso> v{mk("20260827", "1900")};
        const auto s = splitSessions(v);
        check(s.size() == 1 && s.first().start == s.first().end,
              "a single QSO is a session whose start equals its end");
    }

    // ── The threshold is a parameter, so it can become a setting ───────
    {
        QVector<Qso> v{mk("20260827", "1400"), mk("20260827", "1700")};
        check(splitSessions(v, 2).size() == 2, "3 h splits at a 2 h threshold");
        check(splitSessions(v, 6).size() == 1, "3 h does NOT split at a 6 h threshold");
    }

    if (g_failures == 0) {
        std::printf("qso_session_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "qso_session_test: %d failure(s)\n", g_failures);
    return 1;
}
