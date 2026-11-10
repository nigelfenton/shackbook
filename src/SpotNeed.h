#pragma once

// SpotNeed — "would working this spot add anything to the log?"
//
// A cluster spot gives a callsign, a frequency and (sometimes) a mode. The log
// knows which DXCC entities have been worked, and on which band and mode. This
// header joins the two and answers with one of:
//
//   Atno      never worked this entity, on any band or mode
//   NewBand   worked the entity, never on this band
//   NewMode   worked the entity, never in this mode
//   NewSlot   worked the entity on this band and in this mode, but not together
//   Worked    nothing new
//   Unknown   the callsign does not resolve, or there is no log to compare
//
// ⛔ THE ENTITY KEY IS THE WHOLE PROBLEM, so it is stated plainly here.
//
// A spot carries a CALLSIGN; the log stores a country name and a DXCC number
// per QSO. Matching on either of those stored fields is wrong, measured on a
// real 16,538-QSO log (2026-11-09):
//
//   • the stored COUNTRY name matched cty.dat for only 90.3% of QSOs, and is
//     ambiguous in both directions — DXCC 291 appears as both "United States"
//     and "Alaska"; "Italy", "France" and "Madeira Is." each span two
//     entities. Badging from it would call a worked entity an ATNO.
//   • the stored DXCC NUMBER is 100% populated in that log, but cty.dat
//     carries no entity numbers at all (name, zones, continent, lat/lon,
//     prefix — nothing else), so a spot's callsign cannot be turned into one
//     without an external table we do not ship.
//
// So BOTH SIDES ARE RESOLVED THROUGH THE SAME CtyDat::lookup(). The log's own
// callsigns are re-resolved when the index is built, exactly as a spot's call
// is resolved when it arrives. The two agree by construction, whatever cty.dat
// happens to call the place, and no mapping table is needed. 99.92% of that
// log's callsigns resolve (the rest are busted calls like "0L8HQ" that no
// resolver would match, and they yield Unknown rather than a wrong answer).
//
// This header is deliberately free of SQL and widgets so the rule can be
// tested on its own: see tests/spot_need_test.cpp.

#include "Band.h"

#include <QHash>
#include <QStringList>
#include <QSet>
#include <QString>

namespace ShackBook {

enum class SpotNeed {
    Unknown,
    Atno,
    NewBand,
    NewMode,
    NewSlot,
    Worked,
};

// Short label for a table cell. Empty for Unknown, so a spot we cannot judge
// shows nothing rather than a guess.
inline QString spotNeedLabel(SpotNeed n)
{
    switch (n) {
    case SpotNeed::Atno:     return QStringLiteral("ATNO");
    case SpotNeed::NewBand:  return QStringLiteral("NEW BAND");
    case SpotNeed::NewMode:  return QStringLiteral("NEW MODE");
    case SpotNeed::NewSlot:  return QStringLiteral("NEW SLOT");
    case SpotNeed::Worked:   return QStringLiteral("worked");
    case SpotNeed::Unknown:  break;
    }
    return QString();
}

// Does this status mean the operator gains something by working it?
inline bool spotNeedIsWanted(SpotNeed n)
{
    return n == SpotNeed::Atno || n == SpotNeed::NewBand
        || n == SpotNeed::NewMode || n == SpotNeed::NewSlot;
}

// What the log has, keyed by the entity name CtyDat::lookup() returns.
//
// Built once from the log and then asked about many spots. Populate it with
// add() while scanning QSOs — the caller resolves each QSO's callsign through
// the SAME CtyDat instance it will use for spots (see the note above).
class NeedIndex {
public:
    // One logged QSO. `entity` is CtyDat::Entity::country for the QSO's OWN
    // callsign, not the stored country column. Band and mode are matched
    // case-insensitively; empty values are kept out of the sets so a QSO
    // logged without a mode cannot make every mode look worked.
    void add(const QString& entity, const QString& band, const QString& mode)
    {
        if (entity.isEmpty()) return;
        Slots& s = m_byEntity[entity];
        s.any = true;
        const QString b = band.trimmed().toUpper();
        const QString m = mode.trimmed().toUpper();
        if (!b.isEmpty()) s.bands.insert(b);
        if (!m.isEmpty()) s.modes.insert(m);
        if (!b.isEmpty() && !m.isEmpty()) s.slotSet.insert(b + QLatin1Char('|') + m);
    }

    bool isEmpty() const { return m_byEntity.isEmpty(); }
    int  entityCount() const { return m_byEntity.size(); }

    // What the log holds for one entity, for a tooltip ("worked on 20m, 40m —
    // CW, SSB") and so the sets can be asserted directly.
    //
    // ⚠ These exist because the empty-value guards in add() are otherwise
    // UNOBSERVABLE: status() returns early whenever the spot's own mode is
    // empty, so an empty string sitting in modes can never change a verdict.
    // A mutation run (2026-11-09) removed `if (!m.isEmpty())` and every check
    // still passed. Rather than invent a fixture that could not happen, or
    // delete a guard that keeps the data honest for the next caller, the sets
    // are made visible and asserted for what they are.
    QStringList bandsFor(const QString& entity) const
    {
        const auto it = m_byEntity.constFind(entity);
        if (it == m_byEntity.constEnd()) return {};
        QStringList l(it->bands.cbegin(), it->bands.cend());
        l.sort();
        return l;
    }
    QStringList modesFor(const QString& entity) const
    {
        const auto it = m_byEntity.constFind(entity);
        if (it == m_byEntity.constEnd()) return {};
        QStringList l(it->modes.cbegin(), it->modes.cend());
        l.sort();
        return l;
    }

    // The verdict for one spot.
    //
    // `entity` is CtyDat::Entity::country for the SPOT's callsign; empty means
    // the call did not resolve, which is Unknown rather than a guess. An empty
    // band (a spot off the amateur bands) is also Unknown: we cannot say
    // "new band" about a frequency that is not one. An empty MODE is allowed —
    // cluster spots often lack one — and then only ATNO and NewBand can be
    // reported, never NewMode or NewSlot, because nothing is known to compare.
    SpotNeed status(const QString& entity, const QString& band,
                    const QString& mode) const
    {
        if (entity.isEmpty()) return SpotNeed::Unknown;
        const auto it = m_byEntity.constFind(entity);
        if (it == m_byEntity.constEnd()) return SpotNeed::Atno;
        if (!it->any) return SpotNeed::Atno;

        const QString b = band.trimmed().toUpper();
        if (b.isEmpty()) return SpotNeed::Unknown;
        if (!it->bands.contains(b)) return SpotNeed::NewBand;

        const QString m = mode.trimmed().toUpper();
        if (m.isEmpty()) return SpotNeed::Worked;   // band is held; cannot say more
        if (!it->modes.contains(m)) return SpotNeed::NewMode;
        if (!it->slotSet.contains(b + QLatin1Char('|') + m)) return SpotNeed::NewSlot;
        return SpotNeed::Worked;
    }

    // Convenience for a spot that carries a frequency rather than a band name.
    SpotNeed statusForMhz(const QString& entity, double freqMhz,
                          const QString& mode) const
    {
        return status(entity, bandForMhz(freqMhz), mode);
    }

private:
    struct Slots {
        bool any{false};        // the entity was worked at all
        QSet<QString> bands;    // "20M"
        QSet<QString> modes;    // "CW"
        QSet<QString> slotSet; // "20M|CW" — NOT "slots": Qt #defines that
    };
    QHash<QString, Slots> m_byEntity;
};

} // namespace ShackBook
