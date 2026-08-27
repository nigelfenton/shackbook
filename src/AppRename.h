#pragma once

// AppRename — carry a user's data across the ShackLog -> ShackBook rename.
//
// WHY THIS EXISTS. The application name is not cosmetic on any platform:
// QStandardPaths derives the data directory from it, so
// AppLocalDataLocation is <Local>/<Org>/<AppName>, and QSettings derives
// its registry key or config file the same way. Changing setApplicationName
// therefore moves BOTH, and an existing user's logbooks and settings simply
// stop being found — the app looks freshly installed, with an empty log.
//
// The rename was forced by a name collision: a UK amateur radio logging
// program has used "ShackLog" since at least 2005, holds shacklog.co.uk and
// shacklog.com, and is listed on DXZone. Continuing under that name would
// have been confusing for operators and unfair to them.
//
// WHAT THIS DOES, deliberately conservatively:
//   - COPIES rather than moves the data directory, so a failure part-way
//     through cannot destroy the only copy of somebody's log. The old
//     directory is left untouched and can be deleted by hand once the user
//     is satisfied.
//   - Runs ONCE, and only when the new directory does not already exist.
//     A user who has started logging under the new name must never have
//     that overwritten by stale data from the old one.
//   - Never silently discards anything. Every outcome is reported so the
//     caller can tell the user what happened.
//
// This code is expected to be short-lived: once installed users have run a
// migrated version, it can be deleted. It is kept separate from main() so
// that removal is a file deletion rather than surgery.

#include <QString>
#include <QStringList>

namespace ShackBook {

struct MigrationResult {
    enum class Outcome {
        NothingToDo,      // no old data, or new data already present
        Copied,           // old data copied to the new location
        PartiallyCopied,  // some files copied, some failed — see errors
        Failed            // nothing copied; the old data is still intact
    };

    Outcome     outcome{Outcome::NothingToDo};
    QString     fromDir;
    QString     toDir;
    QStringList copied;
    QStringList errors;

    bool needsTelling() const { return outcome != Outcome::NothingToDo; }
};

// Copy <Local>/<Org>/<oldApp>/ to <Local>/<Org>/<newApp>/ when the latter
// does not yet exist. Safe to call on every start: it is a no-op once the
// new directory is present.
//
// MUST be called BEFORE QSettings or the database is first opened, and
// AFTER setOrganizationName — the paths are derived from the organisation.
MigrationResult migrateAppData(const QString& oldAppName,
                               const QString& newAppName);

// A human-readable summary for a message box. Empty when there is nothing
// worth telling the user.
QString describeMigration(const MigrationResult& r);

} // namespace ShackBook
