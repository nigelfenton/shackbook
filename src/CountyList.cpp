#include "CountyList.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace ShackBook {

namespace {

// Administrative words the Census appends that an operator does not type.
// Kept in one place because normalize() below and tools/gen_counties.py must
// agree exactly — the generator writes the keys this matcher compares against,
// so a change here without the same change there silently stops matching.
const QRegularExpression& adminWordRe()
{
    static const QRegularExpression re(
        QStringLiteral("\\b(COUNTY|PARISH|CITY AND BOROUGH|BOROUGH|CENSUS AREA"
                       "|MUNICIPALITY|MUNICIPIO)\\b"));
    return re;
}

} // namespace

QString CountyList::normalize(const QString& name)
{
    const QString upper = name.trimmed().toUpper();
    if (upper.isEmpty()) return {};

    // An independent city keeps its CITY marker. Baltimore County and
    // Baltimore city are different entities that a QSO party scores
    // separately, as are Fairfax, Franklin, Richmond, Roanoke (VA) and
    // St. Louis (MO). Folding both to one key would merge two counties.
    const bool independentCity = upper.endsWith(QLatin1String(" CITY"))
                              && !upper.contains(QLatin1String("CITY AND BOROUGH"));

    QString s = upper;
    s.remove(adminWordRe());
    if (!independentCity)
        s.remove(QRegularExpression(QStringLiteral("\\bCITY\\b")));

    // "Island" is administrative only where it is the whole suffix of a
    // non-county area — the three USVI districts Census calls "St. Croix
    // Island" and every log calls "Saint Croix". Island County WA and Rock
    // Island County IL are real counties and keep the word.

    s.replace(QRegularExpression(QStringLiteral("\\bSAINTE?\\b")), QStringLiteral("ST"));
    s.replace(QRegularExpression(QStringLiteral("\\bSTE\\b")),     QStringLiteral("ST"));
    s.remove(QLatin1Char('.'));
    s.remove(QLatin1Char('\''));
    s.replace(QLatin1Char('-'), QLatin1Char(' '));
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    s = s.trimmed();

    // Only the three USVI districts are stripped, by name, rather than by a
    // general "ends in ISLAND" rule.  A general rule cannot work here: it has
    // to give the same answer for the STORED Census name ("Rock Island
    // County", which keeps ISLAND because it carries County) and for what an
    // operator actually types ("Rock Island", which has no suffix to key off).
    // Keying off the query text alone turned IL,Rock Island into ROCK.
    static const QRegularExpression usviRe(
        QStringLiteral("^(ST|SAINT) (CROIX|JOHN|THOMAS) ISLAND$"));
    if (usviRe.match(s.simplified()).hasMatch())
        s.remove(QRegularExpression(QStringLiteral("\\s+ISLAND$")));

    if (independentCity && !s.endsWith(QLatin1String(" CITY")))
        s = (s + QStringLiteral(" CITY")).trimmed();
    return s;
}

int CountyList::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    m_counties.clear();
    m_byStateKey.clear();
    m_byState.clear();

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        // STATE|FIPS|Canonical Name|Match Key
        const QStringList f4 = line.split(QLatin1Char('|'));
        if (f4.size() != 4) continue;

        County c;
        c.state = f4[0].trimmed().toUpper();
        c.fips  = f4[1].trimmed();
        c.name  = f4[2].trimmed();
        c.key   = f4[3].trimmed().toUpper();
        if (c.state.isEmpty() || c.key.isEmpty()) continue;

        const int idx = m_counties.size();
        m_counties.append(c);
        m_byStateKey.insert(c.state + QLatin1Char('|') + c.key, idx);
        m_byState[c.state].append(idx);
    }
    return m_counties.size();
}

QVector<CountyList::County> CountyList::countiesIn(const QString& state) const
{
    QVector<County> out;
    const auto it = m_byState.constFind(state.trimmed().toUpper());
    if (it == m_byState.constEnd()) return out;
    out.reserve(it->size());
    for (int i : *it) out.append(m_counties[i]);
    return out;
}

QSet<QString> CountyList::states() const
{
    QSet<QString> out;
    out.reserve(m_byState.size());
    for (auto it = m_byState.constBegin(); it != m_byState.constEnd(); ++it)
        out.insert(it.key());
    return out;
}

CountyList::County CountyList::lookup(const QString& cnty,
                                      const QString& stateColumn) const
{
    const QString raw = cnty.trimmed();
    if (raw.isEmpty()) return {};

    QString state;
    QString name;
    const int comma = raw.indexOf(QLatin1Char(','));
    if (comma >= 0) {
        // Canonical ADIF form, "MD,Anne Arundel". Measured across 6521 real
        // QSOs the prefix never disagreed with the state column, so it is
        // taken as authoritative where present.
        state = raw.left(comma).trimmed().toUpper();
        name  = raw.mid(comma + 1).trimmed();
    } else {
        // Bare county name — recoverable when the QSO carries a state of its
        // own, which is 405 of the 858 bare values in the reference log.
        state = stateColumn.trimmed().toUpper();
        name  = raw;
    }
    // An early-out, not a safety property: with an empty state the key below
    // would be "|ANNE ARUNDEL", which cannot exist since every stored key
    // carries a real state prefix. Removing this line was mutation-tested and
    // changed no observable behaviour — it is kept for clarity and to avoid
    // hashing a string that can never hit.
    if (state.isEmpty() || name.isEmpty()) return {};

    const auto it = m_byStateKey.constFind(state + QLatin1Char('|') + normalize(name));
    if (it == m_byStateKey.constEnd()) return {};
    return m_counties[*it];
}

bool CountyList::contains(const QString& state, const QString& name) const
{
    return m_byStateKey.contains(state.trimmed().toUpper()
                                 + QLatin1Char('|') + normalize(name));
}

} // namespace ShackBook
