#include "CountyProgress.h"

#include <QHash>
#include <QMap>

namespace ShackBook {

namespace {

// The confirmed rule used across ShackBook: LoTW or a QSL card. eQSL is not
// accepted for ARRL awards, and awardsSummary() already draws the line here,
// so the county table draws it in the same place.
bool isConfirmed(const Qso& q)
{
    return q.lotwRcvd == QLatin1String("Y") || q.qslRcvd == QLatin1String("Y");
}

} // namespace

CountyProgressSummary countyProgress(const CountyList& counties,
                                     const QString& stateIn,
                                     const QVector<Qso>& qsos)
{
    CountyProgressSummary out;
    out.state = stateIn.trimmed().toUpper();
    if (out.state.isEmpty() || !counties.isLoaded()) return out;

    // Seed with EVERY county in the state, so a county nobody has worked is
    // present with worked == 0. That is the whole point: a table built only
    // from the log can never show what is missing.
    const QVector<CountyList::County> all = counties.countiesIn(out.state);
    if (all.isEmpty()) return out;            // unknown state

    QHash<QString, int> byKey;                // match key -> index in out.counties
    out.counties.reserve(all.size());
    for (const CountyList::County& c : all) {
        CountyStatus s;
        s.fips = c.fips;
        s.name = c.name;
        s.key  = c.key;
        byKey.insert(c.key, out.counties.size());
        out.counties.append(s);
    }
    out.total = out.counties.size();

    QMap<QString, int> unmatched;             // QMap: sorted, so output is stable
    for (const Qso& q : qsos) {
        const QString raw = q.cnty.trimmed();
        if (raw.isEmpty()) continue;

        // lookup() handles both real forms: canonical "MD,Anne Arundel" and a
        // bare name resolved against the QSO's own state column.
        const CountyList::County c = counties.lookup(raw, q.state);
        if (c.fips.isEmpty() || c.state != out.state) {
            // Only count it as unmatched when it plausibly BELONGS to this
            // state, or every out-of-state QSO in the log would be reported
            // as a problem. A value that resolved cleanly to a different
            // state is simply not this party's business.
            const QString st = raw.contains(QLatin1Char(','))
                             ? raw.section(QLatin1Char(','), 0, 0).trimmed().toUpper()
                             : q.state.trimmed().toUpper();
            if (st == out.state) unmatched[raw]++;
            continue;
        }

        const auto it = byKey.constFind(c.key);
        if (it == byKey.constEnd()) continue;  // in the state but not the list
        CountyStatus& s = out.counties[*it];
        if (s.worked == 0) ++out.worked;
        ++s.worked;
        if (isConfirmed(q) && !s.confirmed) {
            s.confirmed = true;
            ++out.confirmed;
        }
    }

    for (auto it = unmatched.constBegin(); it != unmatched.constEnd(); ++it)
        out.unmatched.append({it.key(), it.value()});
    return out;
}

} // namespace ShackBook
