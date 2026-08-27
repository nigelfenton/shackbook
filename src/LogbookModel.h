#pragma once

// LogbookModel — SQLite-backed ham radio logbook.
//
// Single SQLite database file living under QStandardPaths::AppLocalDataLocation
// (per-user, persists across runs).  One `qsos` table holds all QSO records
// using ADIF-aligned column names; one `settings` table holds operator
// defaults (MY_CALL, MY_GRIDSQUARE, default TX_PWR, current contest mode etc).
//
// Schema is migrated automatically on open() — schema_version pragma drives a
// linear migration chain.  Adding a column == bump the version + add a
// migration step in migrateSchema().
//
// All operations are synchronous; the on-disk DB is small enough (tens of
// thousands of QSOs == a few MB) that operations finish in well under a
// millisecond and a worker thread isn't worth the complexity.

#include "Qso.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVector>

namespace ShackBook {

// Query filter for queryQsos / countQsos / export.  Lives at namespace
// scope rather than nested inside LogbookModel because GCC and Clang
// (correctly, per the C++ standard) reject default arguments that
// reference a nested struct whose default member initializers haven't
// been processed yet — and `int limit{0}` here is one of those.
struct LogbookFilter {
    QString text;
    QString band;
    QString mode;
    QString contestId;               // "<NONE>" filters to non-contest QSOs only
    QString dateFrom;                // ADIF YYYYMMDD inclusive
    QString dateTo;
    bool    lotwUnsentOnly{false};   // only QSOs never uploaded to LoTW
    int     limit{0};
};

class LogbookModel : public QObject {
    Q_OBJECT

public:
    explicit LogbookModel(QObject* parent = nullptr);
    ~LogbookModel() override;

    // Open or create the logbook database.  If `path` is empty, defaults to
    //   <AppLocalDataLocation>/shackbook.sqlite
    bool open(const QString& path = {});
    // Close the database and release the connection so open() can be called
    // again on a different file (multi-log operator switching).
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    QString databasePath() const { return m_db.databaseName(); }
    QString errorString()  const { return m_lastError; }
    // Human-readable notice from open() when something the operator should
    // know about happened (integrity failure, backup restored, damaged file
    // kept in use). Empty when the open was clean.
    QString openNotice()   const { return m_openNotice; }

    // Write a verified backup snapshot of the open logbook (VACUUM INTO a
    // fresh single file — safe against a live WAL database — then verify it
    // opens, passes quick_check, and has a qsos table). Keeps the newest
    // `keep` backups carrying the same tag. Returns the backup path, or an
    // empty string on failure. Used automatically (weekly, and before every
    // schema migration); public so Tools/scripts can snapshot on demand.
    QString writeVerifiedBackup(const QString& tag, int keep);

    // ── CRUD ──────────────────────────────────────────────────────────
    // The `actor` argument (schema v2+) names whoever caused this mutation
    // for the audit log — "desktop", "http-api", "n3fjp:WSJT-X", a station
    // ID, etc.  Default keeps the desktop's call sites working unchanged.
    bool insertQso(Qso& qso, const QString& actor = QStringLiteral("system"));
    bool updateQso(const Qso& qso, const QString& actor = QStringLiteral("system"));
    bool deleteQso(qint64 id, const QString& actor = QStringLiteral("system"));
    Qso  getQso(qint64 id, bool* ok = nullptr) const;

    QVector<Qso> queryQsos(const LogbookFilter& filter = {}) const;
    int          countQsos(const LogbookFilter& filter = {}) const;

    // Most recent QSO with this exact callsign, or an empty Qso (id == -1)
    // if never worked.  The "worked before" tier of callsign lookup —
    // previous name/QTH/grid beats any online source.
    Qso lastQsoWith(const QString& call) const;

    bool isDuplicate(const QString& call,
                     const QString& band,
                     const QString& mode,
                     int windowSeconds = 3600) const;

    // ── Settings ──────────────────────────────────────────────────────
    QString settingValue(const QString& key, const QString& defaultValue = {}) const;
    bool    setSetting(const QString& key, const QString& value);

    QString myCall() const            { return settingValue("MY_CALL"); }
    QString myGridsquare() const      { return settingValue("MY_GRIDSQUARE"); }
    QString myState() const           { return settingValue("MY_STATE"); }
    double  defaultTxPwr() const;
    bool    contestMode() const       { return settingValue("CONTEST_MODE") == "1"; }
    QString contestId() const         { return settingValue("CONTEST_ID"); }

    // ── Awards ────────────────────────────────────────────────────────
    // One-pass scan of the whole log for award-relevant distinct sets.
    // "Confirmed" = LoTW or QSL card received (eQSL is not accepted for
    // ARRL awards).  WAS is validated against the real 50-state list with
    // the ARRL rule that DC counts as MD; out-of-list values are reported
    // in wasBogus rather than silently counted.
    struct AwardsSummary {
        int qsoCount{0};
        QSet<int>     dxccWorked, dxccConfirmed;
        QSet<QString> wasWorked,  wasConfirmed, wasBogus;
        QSet<QString> wacWorked,  wacConfirmed;
        QSet<int>     wazWorked,  wazConfirmed;
        QSet<QString> gridsWorked;            // 4-char squares, any band
    };
    AwardsSummary awardsSummary() const;

    // ── Import ────────────────────────────────────────────────────────
    struct AdifImportResult {
        bool ok{false};       // false == file-level failure; see errorString()
        int  imported{0};
        int  duplicates{0};   // same call+date+band+mode, TIME_ON equal to the minute
        int  invalid{0};      // missing required fields, or insert failure
    };
    AdifImportResult importAdif(const QString& filePath,
                                const QString& actor = QStringLiteral("adif-import"));

    // ── LoTW ──────────────────────────────────────────────────────────
    // Mark a set of QSOs as uploaded (lotw_sent='Y', lotw_sdate=adifDate),
    // one transaction, audited as "lotw-upload". Returns rows changed, -1
    // on failure. Emits qsoUpdated(-1) once (bulk) when anything changed.
    int markLotwSent(const QVector<qint64>& ids, const QString& adifDate);
    // Apply one confirmation from a LoTW report: match call+band+date and
    // time-to-the-minute, set lotw_rcvd='Y'/lotw_rdate. Returns rows
    // changed (0 = no match), -1 on failure. Audited as "lotw-confirm".
    int applyLotwConfirmation(const QString& call, const QString& band,
                              const QString& qsoDate, const QString& timeOn,
                              const QString& qslDate);

    // ── Export ────────────────────────────────────────────────────────
    int exportAdif(const QString& filePath, const LogbookFilter& filter = {}) const;
    int exportCabrillo(const QString& filePath,
                       const QString& contestId,
                       const LogbookFilter& filter = {}) const;

    // ── Static helpers ────────────────────────────────────────────────
    // Map a frequency in MHz to an ADIF band string ("20m", "70cm", ...).
    static QString bandFromFreqMhz(double mhz);

    // Map a TCI / Flex slice mode (USB/LSB/CW/AM/FM/DIGU/DIGL/...) to an
    // ADIF base mode + optional submode.  Submode is empty when not
    // applicable; mode may also be empty for ambiguous digital slots
    // (DIGU/DIGL/DIGI) — the entry form must then prompt the operator.
    static void    adifModeFromTciMode(const QString& tciMode,
                                       QString* adifMode,
                                       QString* adifSubmode);

    // Compose an ADIF "<TAG:length>value " field (trailing space included
    // for readability — ADIF parsers ignore whitespace between fields).
    // Returns empty string if value is empty (omit empty fields per spec).
    static QString adifField(const QString& tag, const QString& value);

signals:
    void qsoAdded(qint64 id);
    void qsoUpdated(qint64 id);
    void qsoDeleted(qint64 id);
    void settingChanged(const QString& key);

private:
    bool migrateSchema();
    int  schemaVersion() const;
    bool quickCheckOk();
    bool verifyBackupFile(const QString& path) const;
    void maybeWeeklyBackup();
    bool quarantineAndRestore(const QString& dbPath);
    bool setSchemaVersion(int v);

    static Qso qsoFromRow(class QSqlQuery& q);
    QString    filterToSql(const LogbookFilter& filter, QVariantList* binds) const;
    bool       importDuplicateExists(const Qso& q) const;

    QSqlDatabase m_db;
    QString      m_lastError;
    QString      m_openNotice;
    QString      m_connectionName;
};

} // namespace ShackBook
