#include "AppRename.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace ShackBook {

namespace {

// The data directory for a given application name, with the organisation
// taken from the running application. QStandardPaths cannot be asked for
// "the path a DIFFERENT app name would give", so this reconstructs it the
// same way Qt does: <writable AppLocalData for us>/../<other app name>.
QString dataDirFor(const QString& appName)
{
    const QString mine =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (mine.isEmpty()) return {};
    // mine == <Local>/<Org>/<CurrentAppName>; swap the last component.
    const QDir parent = QFileInfo(mine).dir();
    return parent.filePath(appName);
}

// Recursive copy. Returns false on the first failure, having recorded it.
bool copyTree(const QDir& from, const QDir& to, QStringList* copied,
              QStringList* errors)
{
    if (!to.exists() && !QDir{}.mkpath(to.absolutePath())) {
        *errors << QCoreApplication::translate(
            "AppRename", "could not create %1").arg(to.absolutePath());
        return false;
    }

    bool allOk = true;
    for (const QFileInfo& fi :
         from.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dest = to.filePath(fi.fileName());
        if (fi.isDir()) {
            if (!copyTree(QDir(fi.absoluteFilePath()), QDir(dest), copied, errors))
                allOk = false;
            continue;
        }
        // Never clobber: if something is already at the destination, leave
        // it and say so. This should not happen (we only run when the
        // directory is absent) but the guard costs nothing and the
        // alternative is overwriting a logbook.
        if (QFile::exists(dest)) {
            *errors << QCoreApplication::translate(
                "AppRename", "%1 already exists — left alone").arg(dest);
            allOk = false;
            continue;
        }
        if (QFile::copy(fi.absoluteFilePath(), dest)) {
            *copied << fi.fileName();
        } else {
            *errors << QCoreApplication::translate(
                "AppRename", "could not copy %1").arg(fi.fileName());
            allOk = false;
        }
    }
    return allOk;
}

} // namespace

MigrationResult migrateAppData(const QString& oldAppName,
                               const QString& newAppName)
{
    MigrationResult r;
    if (oldAppName.isEmpty() || newAppName == oldAppName) return r;

    r.fromDir = dataDirFor(oldAppName);
    r.toDir   = dataDirFor(newAppName);
    if (r.fromDir.isEmpty() || r.toDir.isEmpty()) return r;

    const QDir from(r.fromDir);
    const QDir to(r.toDir);

    // Nothing to carry across, or the user already has data under the new
    // name — in which case leave it strictly alone.
    if (!from.exists() || to.exists()) return r;

    // An empty old directory is not worth reporting.
    if (from.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        return r;

    const bool ok = copyTree(from, to, &r.copied, &r.errors);
    if (r.copied.isEmpty())
        r.outcome = MigrationResult::Outcome::Failed;
    else if (ok)
        r.outcome = MigrationResult::Outcome::Copied;
    else
        r.outcome = MigrationResult::Outcome::PartiallyCopied;
    return r;
}

QString describeMigration(const MigrationResult& r)
{
    using O = MigrationResult::Outcome;
    switch (r.outcome) {
    case O::NothingToDo:
        return {};
    case O::Copied:
        return QCoreApplication::translate("AppRename",
            "ShackLog is now ShackBook — the name clashed with an existing "
            "UK logging program.\n\n"
            "Your %n logbook file(s) and settings have been COPIED to:\n%1\n\n"
            "The originals are untouched at:\n%2\n"
            "Delete that folder once you are happy everything is here.",
            nullptr, r.copied.size()).arg(r.toDir, r.fromDir);
    case O::PartiallyCopied:
        return QCoreApplication::translate("AppRename",
            "ShackLog is now ShackBook, but only SOME of your data could be "
            "copied to:\n%1\n\n"
            "Copied: %2\n\nProblems:\n%3\n\n"
            "Nothing has been deleted — your original data is still at:\n%4")
            .arg(r.toDir, r.copied.join(QStringLiteral(", ")),
                 r.errors.join(QStringLiteral("\n")), r.fromDir);
    case O::Failed:
        return QCoreApplication::translate("AppRename",
            "ShackLog is now ShackBook, but your existing data could NOT be "
            "copied to the new location:\n%1\n\n%2\n\n"
            "Nothing has been deleted. Your logbooks are still at:\n%3\n"
            "You can copy that folder across by hand.")
            .arg(r.toDir, r.errors.join(QStringLiteral("\n")), r.fromDir);
    }
    return {};
}

} // namespace ShackBook
