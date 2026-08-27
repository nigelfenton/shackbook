#include "QsoSession.h"

#include <QTimeZone>

#include <algorithm>

namespace ShackLog {

QDateTime qsoDateTime(const Qso& q)
{
    const QString d = q.qsoDate.trimmed();
    QString t = q.timeOn.trimmed();
    if (d.size() != 8 || t.isEmpty()) return {};

    // ADIF TIME_ON is HHMM or HHMMSS. Pad the seconds rather than rejecting
    // the 4-digit form: it is the common one, and most logs in the wild use it.
    if (t.size() == 4) t += QStringLiteral("00");
    if (t.size() != 6) return {};

    const QDate date = QDate::fromString(d, QStringLiteral("yyyyMMdd"));
    const QTime time = QTime::fromString(t, QStringLiteral("hhmmss"));
    if (!date.isValid() || !time.isValid()) return {};

    // ADIF dates and times are UTC by definition. Saying so explicitly keeps
    // the gap arithmetic correct for an operator whose local day and UTC day
    // disagree — which is the whole reason a plain date filter was rejected.
    return QDateTime(date, time, QTimeZone::utc());
}

QVector<QsoSession> splitSessions(const QVector<Qso>& qsos,
                                  int gapHours,
                                  int* unplaceable)
{
    QVector<QsoSession> out;
    if (unplaceable) *unplaceable = 0;

    // Keep only QSOs we can actually place in time, pairing each with its
    // parsed timestamp so the sort does not re-parse for every comparison.
    QVector<QPair<QDateTime, const Qso*>> timed;
    timed.reserve(qsos.size());
    for (const Qso& q : qsos) {
        const QDateTime dt = qsoDateTime(q);
        if (dt.isValid()) timed.append({dt, &q});
        else if (unplaceable) ++*unplaceable;
    }
    if (timed.isEmpty()) return out;

    std::sort(timed.begin(), timed.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // A gap of MORE THAN gapHours starts a new session; exactly gapHours does
    // not, per the decision on #3. secsTo is signed, but the sort above makes
    // it non-negative here.
    const qint64 gapSecs = qint64(gapHours) * 3600;

    QsoSession cur;
    cur.start = timed.first().first;
    for (int i = 0; i < timed.size(); ++i) {
        if (i > 0 && timed[i - 1].first.secsTo(timed[i].first) > gapSecs) {
            cur.end = timed[i - 1].first;
            out.append(cur);
            cur = QsoSession{};
            cur.start = timed[i].first;
        }
        cur.qsos.append(*timed[i].second);
    }
    cur.end = timed.last().first;
    out.append(cur);

    // Newest first: "where did I get out to tonight" is session 0.
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace ShackLog
