// Radio attribution (issue #2): the radio that made a QSO is recorded in
// qsos.station, survives a save/load round-trip, and reaches ADIF as MY_RIG.
//
// The rules under test — all learned from the manual bench run of 2026-08-05:
//   * a QSO logged while connected carries the radio name
//   * a nickname WINS over the TCI-announced device name (TCI announces the
//     APPLICATION, so "AetherSDR" cannot distinguish an HL2 from a FLEX-6700)
//   * a QSO logged with no radio known is left BLANK, never inherited from a
//     previous radio — a wrong attribution is worse than a missing one
//   * MY_RIG round-trips through export → import
//
// Everything runs against a throwaway log in a QTemporaryDir; the operator's
// real logs are never touched. Each check prints PASS/FAIL.

#include "AdifReader.h"
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

Qso qsoWithRadio(const char* call, const QString& radio)
{
    Qso q;
    q.call    = call;
    q.qsoDate = "20260805";
    q.timeOn  = "061913";
    q.band    = "20m";
    q.freq    = 14.074;
    q.mode    = "SSB";
    q.myCall  = "G0JKN";
    q.station = radio;      // what currentRadioName() resolved at save time
    return q;
}

// Mirrors MainWindow::currentRadioName()'s precedence without pulling in the
// GUI: nickname → announced device → empty. Kept in step with that function;
// if the precedence changes there, this must change too.
QString resolveRadio(bool connected, const QString& nickname, const QString& device)
{
    if (!connected) return {};
    const QString nick = nickname.trimmed();
    if (!nick.isEmpty()) return nick;
    return device.trimmed();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("FAIL  could not create temp dir\n");
        return 1;
    }

    // ── Precedence rules ────────────────────────────────────────────────
    check(resolveRadio(true, "", "AetherSDR") == "AetherSDR",
          "announced device is used when no nickname is set");
    check(resolveRadio(true, "Hermes-Lite 2", "AetherSDR") == "Hermes-Lite 2",
          "nickname WINS over the announced device name");
    check(resolveRadio(false, "Hermes-Lite 2", "AetherSDR").isEmpty(),
          "disconnected yields NO radio, even with a nickname set");
    check(resolveRadio(false, "", "AetherSDR").isEmpty(),
          "disconnected never inherits the last announced device");
    check(resolveRadio(true, "   ", "AetherSDR") == "AetherSDR",
          "a whitespace-only nickname does not mask the device name");
    check(resolveRadio(true, "", "   ").isEmpty(),
          "a whitespace-only device name yields blank, not spaces");

    // ── Persistence ─────────────────────────────────────────────────────
    const QString dbPath = tmp.filePath("radio-attrib.sqlite");
    LogbookModel model;
    if (!model.open(dbPath)) {
        std::printf("FAIL  could not open test log\n");
        return 1;
    }

    Qso qNamed = qsoWithRadio("TEST2", "AetherSDR");
    Qso qNick  = qsoWithRadio("TEST3", "Hermes-Lite 2");
    Qso qBlank = qsoWithRadio("TEST4", QString{});
    // Distinct times so the duplicate guard doesn't reject them on re-import.
    qNick.timeOn  = "062013";
    qBlank.timeOn = "062052";
    const bool saved = model.insertQso(qNamed) && model.insertQso(qNick)
                       && model.insertQso(qBlank);
    check(saved, "three QSOs saved");

    bool ok = false;
    check(model.getQso(qNamed.id, &ok).station == "AetherSDR" && ok,
          "announced device persists to qsos.station");
    check(model.getQso(qNick.id, &ok).station == "Hermes-Lite 2" && ok,
          "nickname persists to qsos.station");
    check(model.getQso(qBlank.id, &ok).station.isEmpty() && ok,
          "a QSO logged with no radio stays blank on reload");

    // ── ADIF round-trip ─────────────────────────────────────────────────
    const QString adifPath = tmp.filePath("radio-attrib.adi");
    const int exported = model.exportAdif(adifPath);
    check(exported == 3, "all three QSOs exported");

    QFile f(adifPath);
    check(f.open(QIODevice::ReadOnly), "exported ADIF is readable");
    const QString adif = QString::fromUtf8(f.readAll());
    f.close();

    check(adif.contains("<MY_RIG:9>AetherSDR", Qt::CaseInsensitive),
          "ADIF carries the radio as MY_RIG");
    check(adif.contains("<MY_RIG:13>Hermes-Lite 2", Qt::CaseInsensitive),
          "ADIF MY_RIG length prefix is correct for a name containing a space");
    // The blank one must emit no MY_RIG at all rather than an empty field.
    check(adif.count("MY_RIG", Qt::CaseInsensitive) == 2,
          "a QSO with no radio emits NO MY_RIG field");

    // Re-import into a second log and confirm the value survives the trip.
    const QString db2Path = tmp.filePath("reimport.sqlite");
    LogbookModel model2;
    if (!model2.open(db2Path)) {
        std::printf("FAIL  could not open re-import log\n");
        return 1;
    }
    const LogbookModel::AdifImportResult res = model2.importAdif(adifPath);
    check(res.ok && res.imported == 3, "all three QSOs re-imported");

    const QVector<Qso> back = model2.queryQsos();
    QString gotNamed, gotNick, gotBlank;
    bool sawBlank = false;
    for (const Qso& q : back) {
        if (q.call == "TEST2") gotNamed = q.station;
        if (q.call == "TEST3") gotNick  = q.station;
        if (q.call == "TEST4") { gotBlank = q.station; sawBlank = true; }
    }
    check(gotNamed == "AetherSDR",     "MY_RIG round-trips (announced device)");
    check(gotNick  == "Hermes-Lite 2", "MY_RIG round-trips (nickname with a space)");
    check(sawBlank && gotBlank.isEmpty(),
          "a QSO with no MY_RIG imports as blank, not as the previous value");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
