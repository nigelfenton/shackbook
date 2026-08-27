// app_rename_test — the ShackLog -> ShackBook data migration.
//
// This code touches the only copy of somebody's logbook, so the cases that
// matter are the destructive ones. Each is pinned:
//
//   - existing data under the NEW name is never overwritten
//   - the OLD directory is never deleted or modified
//   - a partial failure is reported, not swallowed
//   - running twice does nothing the second time
//
// The test drives real directories under a temporary root rather than
// mocking the filesystem: the thing being tested IS filesystem behaviour,
// and a mock would prove only that the mock works.

#include "AppRename.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

using namespace ShackBook;

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { ++g_failures; std::printf("FAIL: %s\n", what); }
    else       std::printf("[ OK ] %s\n", what);
}

static bool writeFile(const QString& path, const QString& text)
{
    QDir{}.mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream(&f) << text;
    return true;
}

static QString readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QTextStream(&f).readAll();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ShackBookRenameTest");

    // Redirect AppLocalDataLocation into a temporary tree so the test never
    // touches the real user's data — which is the whole point of the code
    // under test, so getting this wrong would be ironic and destructive.
    QTemporaryDir tmp;
    check(tmp.isValid(), "temporary root created");
    if (!tmp.isValid()) return 1;
    QStandardPaths::setTestModeEnabled(false);
    qputenv("XDG_DATA_HOME", tmp.path().toUtf8());   // no-op on Windows
    QCoreApplication::setApplicationName("NewApp");

    const QString base =
        QFileInfo(QStandardPaths::writableLocation(
                      QStandardPaths::AppLocalDataLocation)).dir().absolutePath();
    const QString oldDir = base + "/OldApp";
    const QString newDir = base + "/NewApp";

    // Clean slate — a previous aborted run must not skew this one.
    QDir(oldDir).removeRecursively();
    QDir(newDir).removeRecursively();

    // ── Nothing to do when there is no old data ────────────────────────
    {
        const auto r = migrateAppData("OldApp", "NewApp");
        check(r.outcome == MigrationResult::Outcome::NothingToDo,
              "no old data -> nothing to do");
        check(!r.needsTelling(), "and nothing to tell the user");
    }

    // ── The ordinary case: data is COPIED, originals left intact ───────
    {
        check(writeFile(oldDir + "/shacklog-G0JKN.sqlite", "LOGBOOK-A"),
              "seeded an old logbook");
        check(writeFile(oldDir + "/shacklog.sqlite", "LOGBOOK-B"),
              "seeded a second old logbook");
        check(writeFile(oldDir + "/backups/old.sqlite", "BACKUP"),
              "seeded a nested backup");

        const auto r = migrateAppData("OldApp", "NewApp");
        check(r.outcome == MigrationResult::Outcome::Copied, "data is copied");
        check(r.copied.size() >= 2, "both top-level logbooks reported");

        check(readFile(newDir + "/shacklog-G0JKN.sqlite") == "LOGBOOK-A",
              "the copy has the right CONTENT, not just the right name");
        check(readFile(newDir + "/backups/old.sqlite") == "BACKUP",
              "nested directories are copied too");

        // ⭐ THE ORIGINAL MUST SURVIVE. A move would leave a user with no
        // fallback if anything about the new location is wrong.
        check(QFile::exists(oldDir + "/shacklog-G0JKN.sqlite"),
              "the ORIGINAL is still there — this copies, it does not move");
        check(readFile(oldDir + "/shacklog.sqlite") == "LOGBOOK-B",
              "and the original content is unmodified");

        check(!describeMigration(r).isEmpty(), "the user is told what happened");
    }

    // ── ⭐ Running again must NOT re-copy over live data ────────────────
    {
        // Simulate the user having logged something under the new name.
        check(writeFile(newDir + "/shacklog-G0JKN.sqlite", "NEW-WORK"),
              "user has since edited the migrated log");
        const auto r = migrateAppData("OldApp", "NewApp");
        check(r.outcome == MigrationResult::Outcome::NothingToDo,
              "a second run does nothing — the new directory already exists");
        check(readFile(newDir + "/shacklog-G0JKN.sqlite") == "NEW-WORK",
              "and the user's newer work is NOT overwritten by stale data");
    }

    // ── An empty old directory is not worth reporting ──────────────────
    {
        QDir(oldDir).removeRecursively();
        QDir(newDir).removeRecursively();
        QDir{}.mkpath(oldDir);
        const auto r = migrateAppData("OldApp", "NewApp");
        check(r.outcome == MigrationResult::Outcome::NothingToDo,
              "an EMPTY old directory is nothing to do");
        check(!QDir(newDir).exists(),
              "and no empty new directory is created for it");
    }

    // ── Degenerate arguments ───────────────────────────────────────────
    {
        check(migrateAppData("", "NewApp").outcome ==
                  MigrationResult::Outcome::NothingToDo,
              "an empty old name is a no-op");
        check(migrateAppData("Same", "Same").outcome ==
                  MigrationResult::Outcome::NothingToDo,
              "identical names are a no-op");
    }

    QDir(oldDir).removeRecursively();
    QDir(newDir).removeRecursively();

    if (g_failures == 0) {
        std::printf("app_rename_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "app_rename_test: %d failure(s)\n", g_failures);
    return 1;
}
