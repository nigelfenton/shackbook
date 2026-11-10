// A PROBE, not a unit test: runs the real NeedIndex + real cty.dat against a
// real logbook and prints what it concludes, so the badge can be checked
// against the operator's own log rather than a fixture.
//
// Not registered with ctest — it needs a logbook that only exists on a real
// machine. Build and run it by hand:
//
//   cmake --build build-tests --target spot_need_reallog_probe
//   build-tests\spot_need_reallog_probe.exe <path-to-shacklog.sqlite> [cty.dat]
//
// It asserts nothing about a particular log. It exists because the entity-key
// decision (SpotNeed.h) was made from measurements on a real 16.5k-QSO log,
// and that measurement should be repeatable rather than quoted from a comment.

#include "CtyDat.h"
#include "SpotNeed.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStringList>
#include <QVariant>

#include <cstdio>

using namespace ShackBook;

namespace {

const char* name(SpotNeed n)
{
    switch (n) {
    case SpotNeed::Unknown: return "Unknown";
    case SpotNeed::Atno:    return "ATNO";
    case SpotNeed::NewBand: return "NewBand";
    case SpotNeed::NewMode: return "NewMode";
    case SpotNeed::NewSlot: return "NewSlot";
    case SpotNeed::Worked:  return "Worked";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::printf("usage: %s <shacklog.sqlite> [cty.dat]\n", argv[0]);
        return 2;
    }
    const QString dbPath  = QString::fromLocal8Bit(argv[1]);
    const QString ctyPath = argc > 2 ? QString::fromLocal8Bit(argv[2])
                                     : QStringLiteral("data/cty.dat");

    CtyDat cty;
    const int entities = cty.load(ctyPath);
    std::printf("cty.dat: %d entities from %s\n", entities,
                qPrintable(QFileInfo(ctyPath).absoluteFilePath()));
    if (entities == 0) { std::printf("FAILED to load cty.dat\n"); return 1; }

    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    db.setDatabaseName(dbPath);
    if (!db.open()) { std::printf("FAILED to open %s\n", qPrintable(dbPath)); return 1; }

    // Same three columns LogbookModel::workedSlots() reads, and the same
    // re-resolution of the callsign through cty.dat.
    NeedIndex idx;
    int rows = 0, resolved = 0;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT call, band, mode FROM qsos"
                               " WHERE call IS NOT NULL AND call != ''"))) {
        std::printf("query failed: %s\n", qPrintable(q.lastError().text()));
        return 1;
    }
    while (q.next()) {
        ++rows;
        const QString call = q.value(0).toString().trimmed().toUpper();
        const auto e = cty.lookup(call);
        if (!e.valid) continue;
        ++resolved;
        idx.add(e.country, q.value(1).toString(), q.value(2).toString());
    }

    std::printf("log: %d QSOs, %d resolved (%.2f%%), %d entities indexed\n\n",
                rows, resolved, rows ? 100.0 * resolved / rows : 0.0,
                idx.entityCount());

    struct Case { const char* call; double mhz; const char* mode; };
    static const Case cases[] = {
        {"IK2ABC", 14.025, "CW"},  {"PY2XYZ", 14.025, "CW"},
        {"TT8XX",  14.025, "CW"},  {"VK9XX",  21.025, "CW"},
        {"W1AW",   14.074, "FT8"}, {"G0JKN",   7.100, "SSB"},
        {"EA8ABC", 10.136, "FT8"}, {"ZL1ABC",  3.573, "FT8"},
        {"JA1ABC", 50.313, "FT8"}, {"0L8HQ",  14.025, "CW"},
        {"IK2ABC", 14.025, ""},    {"IK2ABC",   5.000, "CW"},
    };
    for (const auto& c : cases) {
        const auto e = cty.lookup(QString::fromLatin1(c.call));
        const QString entity = e.valid ? e.country : QString();
        const SpotNeed n = idx.statusForMhz(entity, c.mhz,
                                            QString::fromLatin1(c.mode));
        std::printf("  %-9s %8.3f %-4s -> %-8s  [%s]\n", c.call, c.mhz,
                    *c.mode ? c.mode : "-", name(n),
                    entity.isEmpty() ? "unresolved" : qPrintable(entity));
    }
    return 0;
}
