// Durability tests for the v0.4.1 logbook safety layer: verified backups,
// integrity check on open, quarantine + restore, pre-migration snapshots.
//
// Everything runs against throwaway logs in a QTemporaryDir; the operator's
// real logs are never touched. Each check prints PASS/FAIL — run all, fail
// loud, no bare exit codes trusted.

#include "LogbookModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

using namespace ShackLog;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

Qso sampleQso(const char* call)
{
    Qso q;
    q.call = call;
    q.qsoDate = "20260730";
    q.timeOn = "2300";
    q.band = "20m";
    q.freq = 14.074;
    q.mode = "FT8";
    return q;
}

// Overwrite a page in the middle of the file with garbage — enough to fail
// integrity_check, while the header still reads as a SQLite database.
void corrupt(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadWrite);
    f.seek(f.size() / 2);
    f.write(QByteArray(4096, '\xde'));
    f.close();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir root("shacklog-durability-XXXXXX");
    if (!root.isValid()) { std::printf("FAIL temp dir\n"); return 1; }
    const QString dir = root.path();

    // ── 1. fresh log, manual verified backup ─────────────────────────────
    const QString logPath = dir + "/shacklog-TEST.sqlite";
    {
        LogbookModel m;
        check(m.open(logPath), "fresh log opens");
        check(m.openNotice().isEmpty(), "fresh open carries no notice");
        Qso q = sampleQso("G0JKN");
        check(m.insertQso(q), "QSO logs");
        const QString bak = m.writeVerifiedBackup("manual", 3);
        check(!bak.isEmpty() && QFile::exists(bak),
              "manual backup written and verified");
        // Weekly auto backup fired on open (fresh log = no last-backup stamp).
        const QStringList autos = QDir(dir + "/backups")
                                      .entryList({"shacklog-TEST-*-auto.sqlite"});
        if (autos.size() != 1) {
            std::printf("      [debug] backups dir: %s\n",
                        qPrintable(QDir(dir + "/backups")
                                       .entryList(QDir::Files).join(", ")));
            std::printf("      [debug] last error: %s\n",
                        qPrintable(m.errorString()));
        }
        check(autos.size() == 1, "weekly auto backup fired on first open");
        m.close();
    }

    // ── 2. second open inside a week: no second auto backup ──────────────
    {
        LogbookModel m;
        m.open(logPath);
        const QStringList autos = QDir(dir + "/backups")
                                      .entryList({"shacklog-TEST-*-auto.sqlite"});
        check(autos.size() == 1, "no duplicate auto backup within a week");
        m.close();
    }

    // ── 3. corruption with a verified backup: quarantine + restore ───────
    corrupt(logPath);
    {
        LogbookModel m;
        check(m.open(logPath), "corrupt log with backup still opens");
        check(m.openNotice().contains("restored"),
              "restore is reported to the operator");
        const auto qsos = m.queryQsos();
        bool found = false;
        for (const auto& q : qsos) found = found || q.call == "G0JKN";
        check(found, "the backed-up QSO survives the restore");
        check(!QDir(dir + "/quarantine")
                   .entryList(QDir::Files).isEmpty(),
              "damaged files are quarantined, not deleted");
        m.close();
    }

    // ── 4. corruption with NO backup: damaged-continue, fail loud ────────
    const QString lonePath = dir + "/shacklog-LONE.sqlite";
    {
        LogbookModel m;
        m.open(lonePath);
        Qso q = sampleQso("W2NTV");
        m.insertQso(q);
        m.close();
    }
    // Remove every backup of it (the auto one fired on open).
    for (const QString& f : QDir(dir + "/backups")
                                .entryList({"shacklog-LONE-*.sqlite"}))
        QFile::remove(dir + "/backups/" + f);
    corrupt(lonePath);
    {
        LogbookModel m;
        check(m.open(lonePath), "damaged log without backup still opens");
        check(m.openNotice().contains("FAILED"),
              "the damage is reported loudly, not hidden");
        m.close();
    }

    // ── 5. pre-migration backup on a REAL v2 logbook ─────────────────────
    // Fixture: a copy of the operator's genuine pre-v3 log, if present.
    const QString fixture =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (!fixture.isEmpty() && QFile::exists(fixture)) {
        const QString v2Path = dir + "/shacklog-V2FIX.sqlite";
        QFile::copy(fixture, v2Path);
        LogbookModel m;
        check(m.open(v2Path), "real v2 logbook opens and migrates");
        const QStringList pre = QDir(dir + "/backups")
                                    .entryList({"shacklog-V2FIX-*-premigration-v2.sqlite"});
        check(pre.size() == 1, "pre-migration backup written before the v3 bump");
        check(m.queryQsos().size() > 0, "migrated log still holds its QSOs");
        m.close();
    } else {
        std::printf("SKIP  pre-migration case (no v2 fixture path given)\n");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL PASS" : "FAILURES ABOVE");
    return failures == 0 ? 0 : 1;
}
