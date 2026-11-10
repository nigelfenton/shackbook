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

// ─────────────────────── mode normalisation ───────────────────────
//
// ⛔ THE SPOT AND THE LOG SPEAK DIFFERENT MODE VOCABULARIES. Comparing the
// raw strings badges a worked entity as NEW MODE, measured on a real
// 16,538-QSO log (2026-11-10):
//
//   • an N1MM+/DXLog bandmap spot carries USB or LSB; the log stores SSB
//     (AdifReader normalises USB/LSB -> SSB + SUBMODE on import, as ADIF 3
//     wants). 8,887 QSOs — 53.7% of that log — were unreachable by a phone
//     spot: "20m Italy USB" came back NEW MODE against 20m Italy SSB.
//   • a POTA spot carries FT4; the log stores mode=MFSK, submode=FT4. 294
//     QSOs, 131 of them US/20m, were unreachable the same way.
//   • AetherWSPR hard-codes WSPR, which no log here stores as a MODE.
//
// LogbookModel::adifModeFromTciMode() already folds a RADIO's mode string
// (USB/LSB/CWR/NFM/DFM, Hamlib's -D suffix) for exactly this reason — issue
// #16. It is not reused here because it lives behind Qt6::Sql and this header
// is deliberately free of SQL so the rule can be unit-tested on its own; the
// folds below agree with it and the two are asserted against each other in
// tests/spot_need_test.cpp.
//
// ⚠ A FOLD ALONE IS NOT ENOUGH. Folding FT4 -> MFSK would make an FT4 spot
// look worked by an MFSK16 QSO (this log holds one). So the key is base mode
// AND submode on both sides: the log's own submode column is read, and a
// spot's "FT4" becomes MFSK+FT4 rather than bare MFSK.
struct BaseMode {
    QString mode;      // ADIF MODE ("SSB", "MFSK", "CW")
    QString submode;   // ADIF SUBMODE ("USB", "FT4"), empty when there is none
};

// Fold one mode string — from a spot, or from a log's MODE column — to an
// ADIF base mode plus submode. An unrecognised string is passed through
// upper-cased rather than dropped: an unknown mode must not silently become
// "no mode" (adif_mode_map_test's own warning), and a log and a spot that
// both say the same unknown thing should still match.
inline BaseMode baseModeAndSub(const QString& raw, const QString& rawSub = QString())
{
    QString m = raw.trimmed().toUpper();
    QString s = rawSub.trimmed().toUpper();
    if (m.isEmpty()) return {QString(), QString()};

    // Hamlib marks a data mode with "-D" (USB-D, FM-D). Log the carrier.
    if (m.endsWith(QLatin1String("-D")))
        m.chop(2);

    // Phone: USB/LSB/PHONE are all SSB. The SIDEBAND IS DROPPED, not kept as
    // a submode — which is where this fix first went wrong.
    //
    // ⚠ A submode only belongs in the key when it is a mode the operator
    // CHOSE. FT4 and MFSK16 are different modes that ADIF files under MFSK,
    // so working one does not work the other. A sideband is not like that:
    // 20m is USB and 40m is LSB by convention, nobody "works a band on LSB"
    // as an achievement, and the log stores the sideband only sometimes
    // (8,687 bare SSB QSOs vs 200 carrying USB/LSB in the measured log). Key
    // on it and a USB spot asks a question a plain SSB QSO cannot answer, so
    // every phone spot came back NEW MODE — caught by the three checks that
    // failed when this normaliser was first written.
    if (m == QLatin1String("USB") || m == QLatin1String("LSB")
        || m == QLatin1String("PHONE")) {
        return {QStringLiteral("SSB"), QString()};
    }
    // Reverse sidebands are the same emission; ADIF has no separate mode.
    if (m == QLatin1String("CWR") || m == QLatin1String("CWL")
             || m == QLatin1String("CWU")) {
        m = QStringLiteral("CW");
    } else if (m == QLatin1String("RTTYR")) {
        m = QStringLiteral("RTTY");
    } else if (m == QLatin1String("NFM") || m == QLatin1String("DFM")) {
        m = QStringLiteral("FM");
    } else if (m == QLatin1String("SAM")) {
        m = QStringLiteral("AM");
    }
    // MFSK family: ADIF's MODE is MFSK, the flavour is the SUBMODE. A spot
    // that says only "FT4" carries the submode in the mode field.
    else if (m == QLatin1String("FT4") || m == QLatin1String("MFSK16")
             || m == QLatin1String("MFSK32") || m == QLatin1String("JS8")) {
        if (s.isEmpty()) s = m;
        m = QStringLiteral("MFSK");
    }
    // PSK family, same shape.
    else if (m == QLatin1String("PSK31") || m == QLatin1String("PSK63")
             || m == QLatin1String("PSK125")) {
        if (s.isEmpty()) s = m;
        m = QStringLiteral("PSK");
    }
    return {m, s};
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
    void add(const QString& entity, const QString& band, const QString& mode,
             const QString& submode = QString())
    {
        if (entity.isEmpty()) return;
        Slots& s = m_byEntity[entity];
        s.any = true;
        const QString b = band.trimmed().toUpper();
        const BaseMode bm = baseModeAndSub(mode, submode);
        if (!b.isEmpty()) s.bands.insert(b);
        if (!bm.mode.isEmpty()) {
            s.modes.insert(bm.mode);
            // The exact slot the operator holds, submode included, so an FT4
            // QSO does not make an MFSK16 spot look worked (and vice versa).
            if (!bm.submode.isEmpty())
                s.modes.insert(bm.mode + QLatin1Char('/') + bm.submode);
        }
        if (!b.isEmpty() && !bm.mode.isEmpty()) {
            s.slotSet.insert(b + QLatin1Char('|') + bm.mode);
            if (!bm.submode.isEmpty())
                s.slotSet.insert(b + QLatin1Char('|') + bm.mode
                                 + QLatin1Char('/') + bm.submode);
        }
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
    // ⚠ Holds BOTH base modes and "MODE/SUBMODE" composites ("MFSK" and
    // "MFSK/FT4"), because status() must distinguish "worked this mode" from
    // "worked this exact flavour". A caller wanting a human list should drop
    // the entries containing '/'.
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
                    const QString& mode,
                    const QString& submode = QString()) const
    {
        if (entity.isEmpty()) return SpotNeed::Unknown;
        const auto it = m_byEntity.constFind(entity);
        if (it == m_byEntity.constEnd()) return SpotNeed::Atno;
        if (!it->any) return SpotNeed::Atno;

        const QString b = band.trimmed().toUpper();
        if (b.isEmpty()) return SpotNeed::Unknown;
        if (!it->bands.contains(b)) return SpotNeed::NewBand;

        // Normalise the SPOT's mode exactly as the log's was normalised when
        // the index was built, so the two can be compared at all.
        const BaseMode bm = baseModeAndSub(mode, submode);
        if (bm.mode.isEmpty()) return SpotNeed::Worked;  // band held; cannot say more

        // A spot that names a submode is judged on the exact slot; one that
        // names only a base mode is judged on the base mode, because a bare
        // "MFSK" spot says nothing about which flavour.
        const QString modeKey = bm.submode.isEmpty()
            ? bm.mode
            : bm.mode + QLatin1Char('/') + bm.submode;

        if (!it->modes.contains(modeKey)) {
            // Base mode worked, this flavour not: that is a new slot within a
            // mode already held, not a new mode.
            if (!bm.submode.isEmpty() && it->modes.contains(bm.mode))
                return SpotNeed::NewSlot;
            return SpotNeed::NewMode;
        }
        if (!it->slotSet.contains(b + QLatin1Char('|') + modeKey))
            return SpotNeed::NewSlot;
        return SpotNeed::Worked;
    }

    // Convenience for a spot that carries a frequency rather than a band name.
    SpotNeed statusForMhz(const QString& entity, double freqMhz,
                          const QString& mode,
                          const QString& submode = QString()) const
    {
        return status(entity, bandForMhz(freqMhz), mode, submode);
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
