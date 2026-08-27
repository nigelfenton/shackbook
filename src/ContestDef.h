#pragma once

// ContestDef — what a contest IS, in one place (#4).
//
// Today `contest_id` is a bare string: it is stored, indexed and exported to
// Cabrillo, but nothing knows what a given contest actually requires. The
// Cabrillo writer emits whatever happens to be in stx/srx, the entry form
// shows the everyday QSO fields whatever is running, and there is no way to
// ask "which counties does this party have?".
//
// Three roadmap items need that same missing answer — QSO party mode (#4),
// Cabrillo validation, and Super Check Partial — so the definition is built
// once here rather than three times over.
//
// SCOPE, deliberately narrow. This describes a contest well enough to lay
// out an entry form, score a worked/needed table and sanity-check a Cabrillo
// export. It is NOT a scoring engine: multipliers, points-per-QSO and
// dupe rules vary enormously and belong with whatever implements scoring,
// if that is ever wanted. Encoding half a scoring model here would be worse
// than none, because it would look authoritative.

#include <QString>
#include <QStringList>
#include <QVector>

namespace ShackLog {

// What a station sends and receives. A state QSO party's exchange is the
// part that makes it different from everyday logging, and it is what the
// entry layout has to put in front of the operator.
enum class ExchangeField {
    Rst,          // signal report — near-universal, listed so tab order is explicit
    Serial,       // sequential number (stx / srx)
    County,       // "MD,Anne Arundel" — the state-party case, see CountyList
    State,        // US state / Canadian province abbreviation
    Section,      // ARRL/RAC section (Field Day, Sweepstakes)
    Grid,         // Maidenhead, VHF contests
    Name,
    Power,
    Age,
    FreeText      // anything else; carried in stxString / srxString verbatim
};

struct ContestDef {
    // Cabrillo CONTEST: value, and the key this definition is looked up by.
    // Upper case, matching what is stored in qsos.contest_id.
    QString id;
    QString name;                     // human-readable, for menus

    // The exchange, in the order an operator sends it. Tab order in the
    // entry form follows this, so it is a UI contract as much as a data one.
    QVector<ExchangeField> sent;
    QVector<ExchangeField> received;

    // For a state QSO party: the state whose counties are the multiplier,
    // e.g. "MD" for the Maryland-DC party. Empty for contests that are not
    // county-based, which is how a worked/needed county table knows whether
    // it applies at all.
    QString countyState;

    // True when stations OUTSIDE the sponsoring state also work each other.
    // Affects what "needed" means, so it is recorded rather than assumed.
    bool outOfStateWorksOutOfState{false};

    bool isValid() const { return !id.isEmpty(); }
    bool isCountyParty() const { return !countyState.isEmpty(); }
};

// The bundled definitions. Deliberately a small, honest set rather than a
// half-complete directory of every contest: a definition that is present but
// wrong is worse than one that is absent, because the entry form and the
// Cabrillo check would both act on it.
class ContestCatalog {
public:
    // Load from a file path or Qt resource (":/data/contests.dat").
    // Returns the number of definitions loaded (0 == failure).
    int load(const QString& path);

    bool isLoaded() const { return !m_defs.isEmpty(); }
    int  count() const { return m_defs.size(); }

    // Case-insensitive lookup by contest id. Returns an invalid ContestDef
    // (isValid() == false) when the id is not one we have a definition for,
    // which is the normal case for the long tail of contests — callers must
    // fall back to generic behaviour rather than assuming a definition.
    ContestDef find(const QString& contestId) const;

    QVector<ContestDef> all() const { return m_defs; }

    // Just the county-based parties, for the worked/needed table's picker.
    QVector<ContestDef> countyParties() const;

    // Parse / render a single exchange field name, shared with the data file
    // so the two cannot drift.
    static QString      fieldName(ExchangeField f);
    static ExchangeField fieldFromName(const QString& s, bool* ok = nullptr);

private:
    QVector<ContestDef> m_defs;
};

} // namespace ShackLog
