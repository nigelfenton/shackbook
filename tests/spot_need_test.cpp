// SpotNeed — does working this spot add anything to the log? (#43)
//
// ⭐ The cases that matter most, because getting them wrong is worse than
//    showing nothing at all:
//
//   • an entity worked on another band must read NEW BAND, never ATNO — a
//     false ATNO sends the operator chasing something already in the log;
//   • an entity worked on THIS band must never read ATNO or NEW BAND;
//   • a spot with NO mode (most cluster spots) must not be able to claim
//     NEW MODE or NEW SLOT: nothing is known to compare;
//   • a QSO logged without a mode must not make every mode look worked;
//   • an unresolved callsign must read Unknown, not ATNO. "I cannot tell"
//     and "you have never worked this" are different answers.
//
// The entity key is a resolved name from CtyDat, NOT the log's stored country
// column — see the note at the top of src/SpotNeed.h for the measurements
// that forced that choice.

#include "SpotNeed.h"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>

using namespace ShackBook;

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}

const char* name(SpotNeed n)
{
    switch (n) {
    case SpotNeed::Unknown: return "Unknown";
    case SpotNeed::Atno:    return "Atno";
    case SpotNeed::NewBand: return "NewBand";
    case SpotNeed::NewMode: return "NewMode";
    case SpotNeed::NewSlot: return "NewSlot";
    case SpotNeed::Worked:  return "Worked";
    }
    return "?";
}

void checkStatus(const NeedIndex& idx, const QString& entity,
                 const QString& band, const QString& mode,
                 SpotNeed want, const char* message)
{
    const SpotNeed got = idx.status(entity, band, mode);
    if (got != want)
        std::printf("      (wanted %s, got %s)\n", name(want), name(got));
    check(got == want, message);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // A small log: Italy on 20m CW and 40m SSB, Brazil on 20m CW only.
    NeedIndex idx;
    idx.add(QStringLiteral("Italy"),  QStringLiteral("20m"), QStringLiteral("CW"));
    idx.add(QStringLiteral("Italy"),  QStringLiteral("40m"), QStringLiteral("SSB"));
    idx.add(QStringLiteral("Brazil"), QStringLiteral("20m"), QStringLiteral("CW"));

    check(!idx.isEmpty(), "the index holds something");
    check(idx.entityCount() == 2, "two entities were added");

    // ── ATNO ────────────────────────────────────────────────────────────
    checkStatus(idx, QStringLiteral("Chad"), QStringLiteral("20m"),
                QStringLiteral("CW"), SpotNeed::Atno,
                "an entity never worked is ATNO");

    // ── the false-ATNO trap ─────────────────────────────────────────────
    checkStatus(idx, QStringLiteral("Italy"), QStringLiteral("15m"),
                QStringLiteral("CW"), SpotNeed::NewBand,
                "worked on another band is NEW BAND, not ATNO");
    checkStatus(idx, QStringLiteral("Brazil"), QStringLiteral("80m"),
                QStringLiteral("CW"), SpotNeed::NewBand,
                "Brazil on an unworked band is NEW BAND, not ATNO");

    // ── new mode and new slot ───────────────────────────────────────────
    checkStatus(idx, QStringLiteral("Brazil"), QStringLiteral("20m"),
                QStringLiteral("FT8"), SpotNeed::NewMode,
                "a mode never used for that entity is NEW MODE");
    checkStatus(idx, QStringLiteral("Italy"), QStringLiteral("20m"),
                QStringLiteral("SSB"), SpotNeed::NewSlot,
                "band and mode both worked, but not together, is NEW SLOT");

    // ── nothing new ─────────────────────────────────────────────────────
    checkStatus(idx, QStringLiteral("Italy"), QStringLiteral("20m"),
                QStringLiteral("CW"), SpotNeed::Worked,
                "the exact band and mode already worked is Worked");

    // ── a spot with no mode, which is most cluster spots ────────────────
    checkStatus(idx, QStringLiteral("Chad"), QStringLiteral("20m"),
                QString(), SpotNeed::Atno,
                "no mode still reports ATNO (the entity decides it)");
    checkStatus(idx, QStringLiteral("Italy"), QStringLiteral("15m"),
                QString(), SpotNeed::NewBand,
                "no mode still reports NEW BAND (the band decides it)");
    checkStatus(idx, QStringLiteral("Italy"), QStringLiteral("20m"),
                QString(), SpotNeed::Worked,
                "no mode on a worked band is Worked, never NEW MODE or NEW SLOT");

    // ── "I cannot tell" is not "you need it" ────────────────────────────
    checkStatus(idx, QString(), QStringLiteral("20m"), QStringLiteral("CW"),
                SpotNeed::Unknown,
                "an unresolved callsign is Unknown, not ATNO");
    checkStatus(idx, QStringLiteral("Italy"), QString(), QStringLiteral("CW"),
                SpotNeed::Unknown,
                "a frequency on no amateur band is Unknown");

    // ── a QSO logged without a mode must not poison the mode set ────────
    {
        NeedIndex partial;
        partial.add(QStringLiteral("Spain"), QStringLiteral("20m"), QString());
        checkStatus(partial, QStringLiteral("Spain"), QStringLiteral("20m"),
                    QStringLiteral("CW"), SpotNeed::NewMode,
                    "a QSO logged with no mode leaves every mode still needed");
        checkStatus(partial, QStringLiteral("Spain"), QStringLiteral("20m"),
                    QString(), SpotNeed::Worked,
                    "that same entity with no spot mode is Worked on that band");

        // ⚠ status() alone CANNOT see this: it returns early whenever the
        // spot's own mode is empty, so an empty string in the mode set never
        // changes a verdict. A mutation run proved exactly that — deleting
        // `if (!m.isEmpty())` left every other check passing. Assert the set
        // itself, which is the only honest way to hold that guard.
        check(partial.modesFor(QStringLiteral("Spain")).isEmpty(),
              "a QSO with no mode adds NO mode to the set (not an empty string)");
        check(partial.bandsFor(QStringLiteral("Spain"))
                  == QStringList{QStringLiteral("20M")},
              "that QSO's band is still recorded");
    }

    // The same guard on the other side: a QSO with no BAND must not put an
    // empty string in the band set either.
    {
        NeedIndex noBand;
        noBand.add(QStringLiteral("Chad"), QString(), QStringLiteral("CW"));
        check(noBand.bandsFor(QStringLiteral("Chad")).isEmpty(),
              "a QSO with no band adds NO band to the set");
        check(noBand.modesFor(QStringLiteral("Chad"))
                  == QStringList{QStringLiteral("CW")},
              "that QSO's mode is still recorded");
        checkStatus(noBand, QStringLiteral("Chad"), QStringLiteral("20m"),
                    QStringLiteral("CW"), SpotNeed::NewBand,
                    "an entity worked with no band recorded still needs every band");
    }

    // Sets are reported sorted and upper-cased, which is what a tooltip wants.
    check(idx.bandsFor(QStringLiteral("Italy"))
              == (QStringList{QStringLiteral("20M"), QStringLiteral("40M")}),
          "bandsFor is sorted and upper-cased");
    check(idx.modesFor(QStringLiteral("Italy"))
              == (QStringList{QStringLiteral("CW"), QStringLiteral("SSB")}),
          "modesFor is sorted and upper-cased");
    check(idx.bandsFor(QStringLiteral("Chad")).isEmpty(),
          "an unworked entity reports no bands");

    // ── case and whitespace are not a new band ──────────────────────────
    checkStatus(idx, QStringLiteral("Italy"), QStringLiteral("20M"),
                QStringLiteral("cw"), SpotNeed::Worked,
                "band and mode match case-insensitively");
    checkStatus(idx, QStringLiteral("Italy"), QStringLiteral(" 20m "),
                QStringLiteral(" CW "), SpotNeed::Worked,
                "surrounding whitespace does not invent a new band or mode");

    // ── an empty index must not call everything ATNO silently ───────────
    {
        NeedIndex empty;
        check(empty.isEmpty(), "a fresh index is empty");
        checkStatus(empty, QStringLiteral("Italy"), QStringLiteral("20m"),
                    QStringLiteral("CW"), SpotNeed::Atno,
                    "with no log at all, a resolved entity is ATNO");
    }

    // ── frequency convenience ───────────────────────────────────────────
    check(idx.statusForMhz(QStringLiteral("Italy"), 14.025,
                           QStringLiteral("CW")) == SpotNeed::Worked,
          "statusForMhz maps 14.025 to 20m");
    check(idx.statusForMhz(QStringLiteral("Italy"), 21.025,
                           QStringLiteral("CW")) == SpotNeed::NewBand,
          "statusForMhz maps 21.025 to 15m, which is needed");
    check(idx.statusForMhz(QStringLiteral("Italy"), 5.000,
                           QStringLiteral("CW")) == SpotNeed::Unknown,
          "statusForMhz on an out-of-band frequency is Unknown");

    // ── labels ──────────────────────────────────────────────────────────
    check(spotNeedLabel(SpotNeed::Atno) == QLatin1String("ATNO"),
          "ATNO has a label");
    check(spotNeedLabel(SpotNeed::Unknown).isEmpty(),
          "Unknown shows nothing rather than a guess");
    check(spotNeedIsWanted(SpotNeed::NewSlot),
          "NEW SLOT counts as wanted");
    check(!spotNeedIsWanted(SpotNeed::Worked),
          "Worked is not wanted");
    check(!spotNeedIsWanted(SpotNeed::Unknown),
          "Unknown is not wanted");

    std::printf("\nspot_need_test: %d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
