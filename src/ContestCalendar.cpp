#include "ContestCalendar.h"

#include <QFile>
#include <QTextStream>

#include <algorithm>

namespace ShackBook {

namespace {

struct RuleName { Recurrence r; const char* name; };

const RuleName kRuleNames[] = {
    { Recurrence::NthWeekendOfMonth,  "NTH_WEEKEND"  },
    { Recurrence::LastWeekendOfMonth, "LAST_WEEKEND" },
    { Recurrence::NthSaturdayOfMonth, "NTH_SATURDAY" },
    { Recurrence::FixedDates,         "FIXED"        },
};

// The nth Saturday of a month, or an invalid date when the month has fewer
// than n Saturdays (a 5th Saturday exists only in some months).
QDate nthSaturday(int year, int month, int n)
{
    if (n < 1 || n > 5) return {};
    const QDate first(year, month, 1);
    if (!first.isValid()) return {};

    // Qt::Saturday == 6. Step forward to the first Saturday, then add weeks.
    const int offset = (Qt::Saturday - first.dayOfWeek() + 7) % 7;
    const QDate d = first.addDays(offset + 7 * (n - 1));
    return d.month() == month ? d : QDate{};
}

QDate lastSaturday(int year, int month)
{
    const QDate last(year, month, QDate(year, month, 1).daysInMonth());
    if (!last.isValid()) return {};
    const int back = (last.dayOfWeek() - Qt::Saturday + 7) % 7;
    return last.addDays(-back);
}

} // namespace

QString ContestCalendar::ruleName(Recurrence r)
{
    for (const auto& rn : kRuleNames)
        if (rn.r == r) return QString::fromLatin1(rn.name);
    return {};
}

Recurrence ContestCalendar::ruleFromName(const QString& s, bool* ok)
{
    const QString up = s.trimmed().toUpper();
    for (const auto& rn : kRuleNames) {
        if (up == QLatin1String(rn.name)) {
            if (ok) *ok = true;
            return rn.r;
        }
    }
    if (ok) *ok = false;
    return Recurrence::NthWeekendOfMonth;
}

QDate ContestEvent::startIn(int year) const
{
    if (!isValid()) return {};
    switch (rule) {
    case Recurrence::NthWeekendOfMonth:
    case Recurrence::NthSaturdayOfMonth:
        return nthSaturday(year, month, nth);
    case Recurrence::LastWeekendOfMonth:
        return lastSaturday(year, month);
    case Recurrence::FixedDates: {
        const QDate d(year, month, fixedDay);
        return d.isValid() ? d : QDate{};
    }
    }
    return {};
}

QDate ContestEvent::endIn(int year) const
{
    const QDate s = startIn(year);
    if (!s.isValid()) return {};
    // durationDays counts the days the event covers, so a 2-day weekend ends
    // the day after it starts, not two days after.
    return s.addDays(qMax(1, durationDays) - 1);
}

int ContestCalendar::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    m_events.clear();
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        // ContestId|Name|Rule|Month|Nth|DurationDays|FixedDay
        const QStringList f7 = line.split(QLatin1Char('|'));
        if (f7.size() != 7) continue;

        ContestEvent e;
        e.contestId = f7[0].trimmed().toUpper();
        e.name      = f7[1].trimmed();

        bool ruleOk = false;
        e.rule = ruleFromName(f7[2], &ruleOk);
        // An unrecognised rule is REJECTED, not approximated. Guessing at a
        // recurrence would put the event in the wrong weekend and look
        // authoritative doing it.
        if (!ruleOk) continue;

        bool mOk = false, nOk = false, dOk = false, fOk = false;
        e.month        = f7[3].trimmed().toInt(&mOk);
        e.nth          = f7[4].trimmed().toInt(&nOk);
        e.durationDays = f7[5].trimmed().toInt(&dOk);
        e.fixedDay     = f7[6].trimmed().toInt(&fOk);
        if (!mOk || !nOk || !dOk || !fOk) continue;
        if (!e.isValid()) continue;
        if (e.durationDays < 1) continue;

        m_events.append(e);
    }
    return m_events.size();
}

QVector<ContestOccurrence> ContestCalendar::runningOn(const QDate& date) const
{
    QVector<ContestOccurrence> out;
    if (!date.isValid()) return out;

    // Check the previous year too: an event starting 31 December runs into
    // the next year, and asking on 1 January must still find it.
    for (int year : {date.year() - 1, date.year()}) {
        for (const ContestEvent& e : m_events) {
            const QDate s = e.startIn(year);
            if (!s.isValid()) continue;
            const QDate en = e.endIn(year);
            if (date >= s && date <= en)
                out.append({e, s, en});
        }
    }
    return out;
}

QVector<ContestOccurrence> ContestCalendar::upcoming(const QDate& from,
                                                     int limit) const
{
    QVector<ContestOccurrence> out;
    if (!from.isValid() || limit <= 0) return out;

    // This year and next: asking in December must not report "nothing coming
    // up" when January is full of contests.
    for (int year : {from.year(), from.year() + 1}) {
        for (const ContestEvent& e : m_events) {
            const QDate s = e.startIn(year);
            if (!s.isValid() || s < from) continue;
            out.append({e, s, e.endIn(year)});
        }
    }
    std::sort(out.begin(), out.end(),
              [](const ContestOccurrence& a, const ContestOccurrence& b) {
                  if (a.start != b.start) return a.start < b.start;
                  return a.event.name < b.event.name;   // stable, readable order
              });
    if (out.size() > limit) out.resize(limit);
    return out;
}

} // namespace ShackBook
