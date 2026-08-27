#include "ContestDef.h"

#include <QFile>
#include <QTextStream>

namespace ShackLog {

namespace {

struct FieldName { ExchangeField f; const char* name; };

// One table, used in both directions, so a name can never parse to one field
// and render as another.
const FieldName kFieldNames[] = {
    { ExchangeField::Rst,      "RST"     },
    { ExchangeField::Serial,   "SERIAL"  },
    { ExchangeField::County,   "COUNTY"  },
    { ExchangeField::State,    "STATE"   },
    { ExchangeField::Section,  "SECTION" },
    { ExchangeField::Grid,     "GRID"    },
    { ExchangeField::Name,     "NAME"    },
    { ExchangeField::Power,    "POWER"   },
    { ExchangeField::Age,      "AGE"     },
    { ExchangeField::FreeText, "TEXT"    },
};

QVector<ExchangeField> parseFields(const QString& csv, bool* ok)
{
    QVector<ExchangeField> out;
    if (ok) *ok = true;
    const QStringList toks = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& tok : toks) {
        bool fieldOk = false;
        const ExchangeField f = ContestCatalog::fieldFromName(tok.trimmed(), &fieldOk);
        if (!fieldOk) { if (ok) *ok = false; continue; }
        out.append(f);
    }
    return out;
}

} // namespace

QString ContestCatalog::fieldName(ExchangeField f)
{
    for (const auto& fn : kFieldNames)
        if (fn.f == f) return QString::fromLatin1(fn.name);
    return {};
}

ExchangeField ContestCatalog::fieldFromName(const QString& s, bool* ok)
{
    const QString up = s.trimmed().toUpper();
    for (const auto& fn : kFieldNames) {
        if (up == QLatin1String(fn.name)) {
            if (ok) *ok = true;
            return fn.f;
        }
    }
    if (ok) *ok = false;
    return ExchangeField::FreeText;
}

int ContestCatalog::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    m_defs.clear();
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        // ID|Name|Sent|Received|CountyState|OutOfStateWorksOutOfState
        const QStringList f6 = line.split(QLatin1Char('|'));
        if (f6.size() != 6) continue;

        ContestDef d;
        d.id   = f6[0].trimmed().toUpper();
        d.name = f6[1].trimmed();
        if (d.id.isEmpty() || d.name.isEmpty()) continue;

        // A malformed exchange is skipped whole rather than half-loaded: a
        // definition missing one field would lay out an entry form that
        // silently cannot capture part of the exchange, which is worse than
        // falling back to the generic layout.
        bool sentOk = false, rcvdOk = false;
        d.sent     = parseFields(f6[2], &sentOk);
        d.received = parseFields(f6[3], &rcvdOk);
        if (!sentOk || !rcvdOk) continue;

        d.countyState = f6[4].trimmed().toUpper();
        d.outOfStateWorksOutOfState = (f6[5].trimmed() == QLatin1String("1"));
        m_defs.append(d);
    }
    return m_defs.size();
}

ContestDef ContestCatalog::find(const QString& contestId) const
{
    const QString id = contestId.trimmed().toUpper();
    if (id.isEmpty()) return {};
    for (const ContestDef& d : m_defs)
        if (d.id == id) return d;
    return {};
}

QVector<ContestDef> ContestCatalog::countyParties() const
{
    QVector<ContestDef> out;
    for (const ContestDef& d : m_defs)
        if (d.isCountyParty()) out.append(d);
    return out;
}

} // namespace ShackLog
