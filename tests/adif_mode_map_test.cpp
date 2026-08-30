// Radio mode string -> ADIF MODE/SUBMODE.
//
// This mapping is fed by BOTH radio paths, and they speak different
// vocabularies: TCI says NFM/DFM/DIGU, Hamlib says FM/FM-D/USB-D. A string
// that matches neither branch does not fail loudly -- it yields an empty mode
// and the QSO is logged without one, which is a broken record that nothing
// warns about. That is what happened to FM-D on an IC-9700 (issue #16).

#include "LogbookModel.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

using namespace ShackBook;

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++failures;
}

// Assert the whole mapping, because an empty submode is meaningful: only SSB
// carries one, and a stray submode would be as wrong as a missing mode.
void expectMap(const char* input, const char* wantMode, const char* wantSub,
               const char* what)
{
    QString mode, sub;
    LogbookModel::adifModeFromTciMode(QString::fromLatin1(input), &mode, &sub);
    const bool ok = mode == QLatin1String(wantMode) && sub == QLatin1String(wantSub);
    if (!ok) {
        std::printf("      %s -> mode=\"%s\" sub=\"%s\", wanted mode=\"%s\" sub=\"%s\"\n",
                    input, qPrintable(mode), qPrintable(sub), wantMode, wantSub);
    }
    check(ok, what);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ── The regression: Hamlib data modes ────────────────────────────────
    //
    // The load-bearing assertion. Before the fix these produced an EMPTY
    // mode, so a data QSO was written with no MODE field at all.
    expectMap("FM-D", "FM", "", "FM-D logs as FM, not as nothing");
    expectMap("AM-D", "AM", "", "AM-D logs as AM");
    // Same convention on other rigs. Asserted so the fix is the family, not
    // one radio's spelling.
    expectMap("USB-D", "SSB", "USB", "USB-D keeps the SSB/USB pair");
    expectMap("LSB-D", "SSB", "LSB", "LSB-D keeps the SSB/LSB pair");

    // Lower case, because the function upper-cases its input and a caller
    // could reasonably pass either.
    expectMap("fm-d", "FM", "", "the suffix is stripped case-insensitively");

    // ── What already worked must keep working ────────────────────────────
    expectMap("USB", "SSB", "USB", "USB still maps to SSB/USB");
    expectMap("LSB", "SSB", "LSB", "LSB still maps to SSB/LSB");
    expectMap("CW", "CW", "", "CW is unchanged");
    expectMap("CWR", "CW", "", "CWR maps to CW");
    // The reverse-sideband forms, both in this radio's mode list. ADIF has
    // no separate reverse mode, so they fold into the base one.
    expectMap("RTTYR", "RTTY", "", "RTTYR maps to RTTY");
    expectMap("FM", "FM", "", "plain FM is unchanged");
    expectMap("NFM", "FM", "", "TCI's NFM still maps to FM");
    expectMap("AM", "AM", "", "AM is unchanged");
    expectMap("RTTY", "RTTY", "", "RTTY is unchanged");

    // ── Deliberately empty ───────────────────────────────────────────────
    //
    // DIGU/DIGL are left blank ON PURPOSE so the entry form forces a pick:
    // the real digital mode lives in the soundcard program, not in the radio.
    // Asserted so a future "map everything" change cannot quietly undo that
    // decision -- it is a choice, not a gap.
    expectMap("DIGU", "", "", "DIGU stays empty so the operator picks the mode");
    expectMap("DIGL", "", "", "DIGL stays empty for the same reason");

    // A bare "-D" must not become an empty-string mode match. Chopping the
    // suffix leaves nothing, and nothing must not accidentally match a branch.
    expectMap("-D", "", "", "a bare suffix maps to nothing rather than misfiring");
    expectMap("", "", "", "an empty mode stays empty");

    // "D-STAR" ENDS IN NEITHER -- but it contains a dash, and a sloppier fix
    // (strip everything after a dash) would turn it into "D". The IC-9700
    // reports D-STAR, so this is a real string on a real radio here.
    expectMap("D-STAR", "", "", "D-STAR is not mangled by the suffix rule");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
