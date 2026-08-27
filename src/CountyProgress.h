#pragma once

// CountyProgress — worked / needed counties for a state QSO party (#4).
//
// This is the item that changes how you operate: it turns "have I got
// Calvert yet?" from a memory question into a glance.
//
// Both halves already exist. `cnty` and `state` are stored per QSO and
// populated by AdifReader, so WORKED is a query over existing columns;
// CountyList (#5) supplies the counties that EXIST, so NEEDED is the
// difference. `contest_id` is indexed, so scoping to one party is cheap.
//
// Deliberately a pure function over QSOs rather than a database query: it
// has no SQL and no model dependency, which is what lets it be tested
// against a handful of hand-written QSOs instead of a fixture logbook.

#include "CountyList.h"
#include "Qso.h"

#include <QString>
#include <QVector>

namespace ShackBook {

struct CountyStatus {
    QString fips;        // stable identity
    QString name;        // Census name, e.g. "Anne Arundel County"
    QString key;         // normalised match key
    int     worked{0};   // QSOs into this county (0 == still needed)
    bool    confirmed{false};   // LoTW or QSL card received
};

struct CountyProgressSummary {
    QString state;                    // the party's state, e.g. "MD"
    QVector<CountyStatus> counties;   // ALL counties in the state, Census order
    int total{0};
    int worked{0};
    int confirmed{0};

    // County values that did not match anything in the reference list, with
    // how many QSOs carried each. Reported rather than dropped: on the
    // reference logbook these are real mis-logged QSOs (AZ,Kane - Kane is
    // IL/UT; SC,Calvert - Calvert is MD), and silently ignoring them would
    // turn a data-entry error into a wrong "needed" count.
    QVector<QPair<QString, int>> unmatched;

    int needed() const { return total - worked; }
};

// Compute progress for `state` over the QSOs given.
//
// The caller decides which QSOs to pass - all of them, or just one contest's
// - because "worked" means different things for an award (ever) and for a
// party (this weekend), and neither is more correct than the other.
CountyProgressSummary countyProgress(const CountyList& counties,
                                     const QString& state,
                                     const QVector<Qso>& qsos);

} // namespace ShackBook
