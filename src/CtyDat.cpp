#include "CtyDat.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace ShackBook {

// cty.dat format (https://www.country-files.com/cty-dat-format/):
//   Entity header — 8 colon-terminated fields on one line:
//     Name: CQ: ITU: Cont: Lat: Long: TZ: PrimaryPrefix:
//   Followed by continuation lines of comma-separated alias prefixes,
//   terminated by ';'.  Alias tokens may carry overrides:
//     (#) CQ zone override   [#] ITU zone override
//     <lat/long>  {cont}  ~tz~   — positional overrides we ignore
//     =CALL — exact callsign (not a prefix)
//   A '*' before the primary prefix marks WAE-only pseudo-entities; they
//   still resolve to a sensible country name so we keep them.

int CtyDat::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    QTextStream in(&f);

    m_entities.clear();
    m_exact.clear();
    m_prefixes.clear();

    QString aliasBuf;
    int currentIdx = -1;

    auto flushAliases = [this](const QString& buf, int idx) {
        if (idx < 0) return;
        const QStringList toks =
            buf.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString tok : toks) {
            tok = tok.trimmed();
            if (tok.isEmpty()) continue;

            Ref ref;
            ref.idx = idx;

            // Strip / capture overrides in any order.
            auto extract = [&tok](QChar open, QChar close) -> QString {
                const int a = tok.indexOf(open);
                if (a < 0) return {};
                const int b = tok.indexOf(close, a + 1);
                if (b < 0) return {};
                const QString inner = tok.mid(a + 1, b - a - 1);
                tok.remove(a, b - a + 1);
                return inner;
            };
            const QString cq  = extract(QLatin1Char('('), QLatin1Char(')'));
            const QString itu = extract(QLatin1Char('['), QLatin1Char(']'));
            extract(QLatin1Char('<'), QLatin1Char('>'));   // lat/long — ignored
            extract(QLatin1Char('{'), QLatin1Char('}'));   // continent — ignored
            extract(QLatin1Char('~'), QLatin1Char('~'));   // local time — ignored
            if (!cq.isEmpty())  ref.cqzOverride  = cq.toInt();
            if (!itu.isEmpty()) ref.ituzOverride = itu.toInt();

            if (tok.startsWith(QLatin1Char('='))) {
                m_exact.insert(tok.mid(1).toUpper(), ref);
            } else if (!tok.isEmpty()) {
                m_prefixes.insert(tok.toUpper(), ref);
            }
        }
    };

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;

        const bool isHeader = !line.startsWith(QLatin1Char(' '))
                           && !line.startsWith(QLatin1Char('\t'))
                           && line.contains(QLatin1Char(':'));
        if (isHeader) {
            const QStringList parts = line.split(QLatin1Char(':'));
            if (parts.size() < 8) continue;          // malformed — skip
            Entity e;
            e.country = parts[0].trimmed();
            e.cqz     = parts[1].trimmed().toInt();
            e.ituz    = parts[2].trimmed().toInt();
            e.cont    = parts[3].trimmed();
            e.valid   = true;
            m_entities.append(e);
            currentIdx = m_entities.size() - 1;

            QString primary = parts[7].trimmed();
            if (primary.startsWith(QLatin1Char('*')))
                primary = primary.mid(1);
            if (!primary.isEmpty()) {
                Ref ref; ref.idx = currentIdx;
                m_prefixes.insert(primary.toUpper(), ref);
            }
            aliasBuf.clear();
        } else if (currentIdx >= 0) {
            aliasBuf += line.trimmed();
            if (aliasBuf.endsWith(QLatin1Char(';'))) {
                aliasBuf.chop(1);
                flushAliases(aliasBuf, currentIdx);
                aliasBuf.clear();
            }
        }
    }
    return m_entities.size();
}

CtyDat::Entity CtyDat::lookup(const QString& callIn) const
{
    Entity none;
    const QString call = callIn.trimmed().toUpper();
    if (call.isEmpty() || m_entities.isEmpty()) return none;

    auto resolve = [this](const Ref& ref) {
        Entity e = m_entities.value(ref.idx);
        if (ref.cqzOverride)  e.cqz  = ref.cqzOverride;
        if (ref.ituzOverride) e.ituz = ref.ituzOverride;
        return e;
    };

    // Exact-call override (covers special events, club calls, oddities).
    const auto ex = m_exact.constFind(call);
    if (ex != m_exact.constEnd()) return resolve(ex.value());

    // Longest-prefix match.
    for (int len = call.size(); len >= 1; --len) {
        const auto it = m_prefixes.constFind(call.left(len));
        if (it != m_prefixes.constEnd()) return resolve(it.value());
    }
    return none;
}

} // namespace ShackBook
