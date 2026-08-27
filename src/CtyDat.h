#pragma once

// CtyDat — offline callsign-prefix → country/continent/zone resolution using
// AD1C's cty.dat country file (bundled as a Qt resource, data/cty.dat;
// refresh it from https://www.country-files.com/ now and then).
//
// This is the "possible location" tier of the lookup chain: instant, offline,
// Field-Day-proof.  Longest-prefix match with exact-callsign overrides and
// per-prefix CQ/ITU zone overrides, per the cty.dat format.

#include <QHash>
#include <QString>

namespace ShackBook {

class CtyDat {
public:
    struct Entity {
        QString country;
        QString cont;        // NA/SA/EU/AF/AS/OC/AN
        int     cqz{0};
        int     ituz{0};
        bool    valid{false};
    };

    // Load from a file path or Qt resource (":/data/cty.dat").
    // Returns number of entities loaded (0 == failure).
    int load(const QString& path);

    bool isLoaded() const { return !m_entities.isEmpty(); }
    int  entityCount() const { return m_entities.size(); }

    // Resolve a callsign. Exact-call overrides win, then the longest
    // matching prefix. Returns Entity with valid=false when unknown.
    Entity lookup(const QString& call) const;

private:
    struct Ref {                 // entity index + optional zone overrides
        int idx{-1};
        int cqzOverride{0};
        int ituzOverride{0};
    };
    QVector<Entity> m_entities;
    QHash<QString, Ref> m_exact;     // "=W1AW" style entries (stored bare)
    QHash<QString, Ref> m_prefixes;  // ordinary prefixes
};

} // namespace ShackBook
