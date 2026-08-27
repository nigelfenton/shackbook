#pragma once

// ContestCalendar — what is on now, and what is coming up (#4, item 3).
//
// WHY THIS COMPUTES DATES RATHER THAN LISTING THEM.
//
// The obvious design is a bundled table of dates. It is also wrong, and in a
// way that bites exactly the machine this feature is for: contest schedules
// are RULES, not dates. The Maryland-DC party is "the second full weekend of
// August"; California is "the first full weekend of October". A bundled list
// of 2026 dates is silently stale in 2027 — and the box that most needs to
// know what is on is often the one at a field site with no way to refresh it.
//
// So the rule is what ships, and the dates are computed for whatever year is
// asked about. That makes the calendar correct in years nobody has updated
// the file, which is the only property that matters offline.
//
// SCOPE. This answers "what is on, and when". It does not know about
// exchanges — ContestDef does that, and the two are joined by contest id.
// Some events (many state parties) have a break overnight; that detail is
// NOT modelled here, because "is it running right now" at 03:00 local is a
// question the operator can answer better than a simplified rule can.

#include <QDate>
#include <QString>
#include <QVector>

namespace ShackLog {

// How an event recurs. Deliberately a small set: these cover the state
// parties and the majority of the big contests, and an unrecognised rule is
// rejected at load rather than approximated into the wrong weekend.
enum class Recurrence {
    NthWeekendOfMonth,     // "2nd full weekend of August" — n = 2, month = 8
    LastWeekendOfMonth,    // "last full weekend of October"
    NthSaturdayOfMonth,    // single-day events anchored to a Saturday
    FixedDates             // same calendar dates every year (rare, e.g. an anniversary)
};

struct ContestEvent {
    QString    contestId;      // joins to ContestDef; may be empty for events
                               // we can date but have no exchange definition for
    QString    name;
    Recurrence rule{Recurrence::NthWeekendOfMonth};
    int        month{0};       // 1-12
    int        nth{0};         // 1-5 for Nth rules; ignored for Last/Fixed
    int        durationDays{2};// 2 for a weekend, 1 for a single day
    int        fixedDay{0};    // FixedDates only

    bool isValid() const { return !name.isEmpty() && month >= 1 && month <= 12; }

    // The event's start date in `year`. Invalid QDate if the rule cannot be
    // resolved (e.g. a 5th weekend in a month that has only four).
    QDate startIn(int year) const;
    QDate endIn(int year) const;
};

// An occurrence resolved to real dates, which is what a caller displays.
struct ContestOccurrence {
    ContestEvent event;
    QDate        start;
    QDate        end;

    bool runsOn(const QDate& d) const { return d >= start && d <= end; }
    int  daysUntil(const QDate& from) const { return from.daysTo(start); }
};

class ContestCalendar {
public:
    // Load from a file path or Qt resource (":/data/calendar.dat").
    // Returns the number of events loaded (0 == failure).
    int load(const QString& path);

    bool isLoaded() const { return !m_events.isEmpty(); }
    int  count() const { return m_events.size(); }

    // Events running on `date`. Usually empty, occasionally more than one.
    QVector<ContestOccurrence> runningOn(const QDate& date) const;

    // The next `limit` occurrences starting after `from`, soonest first.
    // Looks into next year so late-December does not report an empty list —
    // "nothing coming up" in December would be wrong, not merely unhelpful.
    QVector<ContestOccurrence> upcoming(const QDate& from, int limit = 5) const;

    static QString      ruleName(Recurrence r);
    static Recurrence   ruleFromName(const QString& s, bool* ok = nullptr);

private:
    QVector<ContestEvent> m_events;
};

} // namespace ShackLog
