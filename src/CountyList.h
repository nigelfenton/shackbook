#pragma once

// CountyList — offline US county / parish / borough reference, so a
// worked/needed table can show NEEDED rather than only WORKED (issue #5).
//
// ShackBook already stores `cnty` and `state` per QSO, which answers "which
// counties have I worked?".  Answering "which do I still need?" needs the
// list of counties that EXIST, which is what this bundles: the US Census
// 2020 FIPS county file (public domain), shipped as a Qt resource the same
// way cty.dat is, so it works at a field site with no internet.
//
// Matching is deliberately strict and reports what it could not place, in
// the same spirit as LogbookModel::awardsSummary()'s wasBogus set: a county
// list that silently swallows a mis-logged QSO turns a data-entry error into
// a wrong award count.

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace ShackBook {

class CountyList {
public:
    struct County {
        QString state;      // "MD"
        QString fips;       // "24003" — stable identity, unlike the name
        QString name;       // "Anne Arundel County" — Census name, verbatim
        QString key;        // "ANNE ARUNDEL" — normalised match key
    };

    // Load from a file path or Qt resource (":/data/counties.dat").
    // Returns number of counties loaded (0 == failure).
    int load(const QString& path);

    bool isLoaded() const { return !m_counties.isEmpty(); }
    int  countyCount() const { return m_counties.size(); }

    // Every county in a state, in Census name order. Empty for an unknown
    // state. The state code is case-insensitive.
    QVector<County> countiesIn(const QString& state) const;

    // States the list knows about, uppercase.
    QSet<QString> states() const;

    // Normalise an operator-entered or ADIF county name to the match key:
    // uppercased, administrative word removed (County/Parish/Borough/
    // Municipio/Municipality/Census Area), punctuation stripped, Saint
    // folded to ST.  Independent cities keep a trailing CITY, because six
    // of them share a name with a county in the same state.
    static QString normalize(const QString& name);

    // Resolve an ADIF CNTY value to a county.  Accepts both forms found in
    // real logs: canonical "MD,Anne Arundel" and a bare "Anne Arundel" with
    // the state supplied separately from the QSO's own state column.
    // Returns a County with an empty fips when unmatched.
    County lookup(const QString& cnty, const QString& stateColumn = {}) const;

    // True when lookup() would resolve. Convenience for filters.
    bool contains(const QString& state, const QString& name) const;

private:
    QVector<County>            m_counties;
    QHash<QString, int>        m_byStateKey;   // "MD|ANNE ARUNDEL" -> index
    QHash<QString, QVector<int>> m_byState;    // "MD" -> indices, in file order
};

} // namespace ShackBook
