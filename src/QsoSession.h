#pragma once

// QsoSession — split a run of QSOs into operating sessions (#3).
//
// There is no session column in the schema; a session is DERIVED. The rule,
// decided on the issue: a gap of MORE THAN 2 hours between consecutive
// contacts starts a new session. A gap of exactly 2 hours does not.
//
// Two hours rather than the 4-6 first floated, because it keeps an evening
// that crosses midnight UTC in one piece (which a plain date filter gets
// wrong, and which lands mid-evening for US operators) while still splitting
// an afternoon session from an evening one.
//
// Known trade, recorded on the issue: a quiet band is not a session boundary
// in the operator's head but is one by this rule. Two hours with nothing
// logged genuinely is a break, so that is the right first version; the
// threshold is a parameter here so making it a setting later is trivial.
//
// Contact time comes from qso_date + time_on. Where a log was typed up after
// the fact those are still the times worked, which is what an operator means
// by "where did I get out to tonight" — created_at would instead group by
// when the typing happened and read as one long session.

#include "Qso.h"

#include <QDateTime>
#include <QVector>

namespace ShackBook {

struct QsoSession {
    // The session's own QSOs, in contact-time order. Carried by value rather
    // than as indices into the caller's vector: this function sorts
    // internally, so an index would refer to the sorted order and silently
    // mismatch the vector the caller still holds.
    QVector<Qso> qsos;
    QDateTime    start;         // UTC, time of the first contact
    QDateTime    end;           // UTC, time of the last contact

    int count() const { return qsos.size(); }
};

// Parse ADIF qso_date (YYYYMMDD) + time_on (HHMM or HHMMSS) as UTC.
// Returns an invalid QDateTime when either field is missing or malformed —
// callers must decide what to do with unplaceable QSOs rather than having a
// silent epoch-zero sneak into a session boundary.
QDateTime qsoDateTime(const Qso& q);

// Split into sessions, newest session FIRST (index 0 is the most recent),
// which is the order a "show me tonight" view wants.
//
// `qsos` need not be sorted; it is ordered internally by contact time.
// QSOs whose time cannot be parsed are excluded from every session and
// counted in `unplaceable` — never silently folded into an adjacent one.
QVector<QsoSession> splitSessions(const QVector<Qso>& qsos,
                                  int gapHours = 2,
                                  int* unplaceable = nullptr);

} // namespace ShackBook
