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

    // ── mode vocabularies: the spot and the log do not use the same words ──
    //
    // These are the cases the first version of this suite could not fail,
    // because every check used CW/SSB/FT8 on BOTH sides. Each one below was a
    // measured wrong badge on a real 16,538-QSO log (2026-11-10).
    {
        NeedIndex idx;
        // How the log actually stores things: phone as SSB(+sideband
        // submode), FT4 as MFSK/FT4. 8,887 of that log's QSOs are SSB.
        idx.add(QStringLiteral("Italy"), QStringLiteral("20m"),
                QStringLiteral("SSB"));
        idx.add(QStringLiteral("Spain"), QStringLiteral("20m"),
                QStringLiteral("SSB"), QStringLiteral("USB"));
        idx.add(QStringLiteral("Chad"),  QStringLiteral("20m"),
                QStringLiteral("MFSK"), QStringLiteral("FT4"));
        idx.add(QStringLiteral("Japan"), QStringLiteral("20m"),
                QStringLiteral("MFSK"), QStringLiteral("MFSK16"));

        // An N1MM+/DXLog bandmap spot carries USB, not SSB.
        check(idx.status(QStringLiteral("Italy"), QStringLiteral("20m"),
                         QStringLiteral("USB")) == SpotNeed::Worked,
              "a USB spot is Worked against a log that stores SSB");
        check(idx.status(QStringLiteral("Italy"), QStringLiteral("20m"),
                         QStringLiteral("LSB")) == SpotNeed::Worked,
              "an LSB spot is Worked against a log that stores SSB");
        // POTA says PHONE.
        check(idx.status(QStringLiteral("Italy"), QStringLiteral("20m"),
                         QStringLiteral("PHONE")) == SpotNeed::Worked,
              "a POTA PHONE spot is Worked against a logged SSB QSO");
        // Hamlib's data suffix: the carrier is what gets logged (issue #16).
        check(idx.status(QStringLiteral("Italy"), QStringLiteral("20m"),
                         QStringLiteral("USB-D")) == SpotNeed::Worked,
              "USB-D folds to SSB rather than reading as a new mode");
        // A log that recorded the sideband is still matched by a bare SSB spot.
        check(idx.status(QStringLiteral("Spain"), QStringLiteral("20m"),
                         QStringLiteral("SSB")) == SpotNeed::Worked,
              "a bare SSB spot is Worked against a logged SSB/USB QSO");
        check(idx.status(QStringLiteral("Spain"), QStringLiteral("20m"),
                         QStringLiteral("USB")) == SpotNeed::Worked,
              "a USB spot is Worked against a logged SSB/USB QSO too");

        // A POTA FT4 spot against an FT4 QSO stored as MFSK/FT4.
        check(idx.status(QStringLiteral("Chad"), QStringLiteral("20m"),
                         QStringLiteral("FT4")) == SpotNeed::Worked,
              "an FT4 spot is Worked against a log that stores MFSK/FT4");

        // The fold must NOT go too far: FT4 and MFSK16 are both MFSK, and
        // one does not make the other worked.
        check(idx.status(QStringLiteral("Japan"), QStringLiteral("20m"),
                         QStringLiteral("FT4")) == SpotNeed::NewSlot,
              "FT4 is NOT worked by an MFSK16 QSO - a fold alone would say it was");
        check(idx.status(QStringLiteral("Chad"), QStringLiteral("20m"),
                         QStringLiteral("MFSK16")) == SpotNeed::NewSlot,
              "MFSK16 is NOT worked by an FT4 QSO");

        // Reverse-sideband CW is the same emission.
        NeedIndex cw;
        cw.add(QStringLiteral("Peru"), QStringLiteral("40m"), QStringLiteral("CW"));
        check(cw.status(QStringLiteral("Peru"), QStringLiteral("40m"),
                        QStringLiteral("CWR")) == SpotNeed::Worked,
              "CWR folds to CW");
        check(cw.status(QStringLiteral("Peru"), QStringLiteral("40m"),
                        QStringLiteral("RTTY")) == SpotNeed::NewMode,
              "a genuinely new mode is still NEW MODE after normalisation");

        // An unrecognised mode must not become "no mode": a WSPR spot against
        // a log with no WSPR is a real NEW MODE, not a silent Worked.
        check(cw.status(QStringLiteral("Peru"), QStringLiteral("40m"),
                        QStringLiteral("WSPR")) == SpotNeed::NewMode,
              "an unknown mode (WSPR) is NEW MODE, not silently swallowed");
        NeedIndex w;
        w.add(QStringLiteral("Peru"), QStringLiteral("40m"), QStringLiteral("WSPR"));
        check(w.status(QStringLiteral("Peru"), QStringLiteral("40m"),
                       QStringLiteral("WSPR")) == SpotNeed::Worked,
              "an unknown mode still matches ITSELF (passed through, not dropped)");
    }

    // ── the normaliser on its own ───────────────────────────────────────
    check(baseModeAndSub(QStringLiteral("usb")).mode == QLatin1String("SSB"),
          "baseModeAndSub is case-insensitive");
    // The sideband is DROPPED, not kept: see the note in SpotNeed.h. Keying
    // on it made every phone spot NEW MODE against a plain SSB log.
    check(baseModeAndSub(QStringLiteral("USB")).submode.isEmpty(),
          "USB drops the sideband rather than keying on it");
    check(baseModeAndSub(QStringLiteral("LSB")).mode == QLatin1String("SSB")
       && baseModeAndSub(QStringLiteral("LSB")).submode.isEmpty(),
          "LSB likewise folds to bare SSB");
    check(baseModeAndSub(QStringLiteral("FT4")).mode == QLatin1String("MFSK")
       && baseModeAndSub(QStringLiteral("FT4")).submode == QLatin1String("FT4"),
          "FT4 becomes MFSK/FT4, not bare MFSK");
    check(baseModeAndSub(QStringLiteral("FM-D")).mode == QLatin1String("FM"),
          "Hamlib's -D suffix is stripped to the carrier");
    check(baseModeAndSub(QStringLiteral("")).mode.isEmpty(),
          "an empty mode stays empty");
    check(baseModeAndSub(QStringLiteral("  cw  ")).mode == QLatin1String("CW"),
          "surrounding whitespace is trimmed");

    // humanModesFor() drops the composites that status() needs.
    {
        NeedIndex h;
        h.add(QStringLiteral("Japan"), QStringLiteral("20m"),
              QStringLiteral("SSB"), QStringLiteral("USB"));
        h.add(QStringLiteral("Japan"), QStringLiteral("20m"),
              QStringLiteral("RTTY"), QStringLiteral("ASCI"));
        check(h.modesFor(QStringLiteral("Japan")).contains(QStringLiteral("SSB/USB")),
              "modesFor keeps the composite status() needs");
        const QStringList human = h.humanModesFor(QStringLiteral("Japan"));
        check(human == QStringList({QStringLiteral("RTTY"), QStringLiteral("SSB")}),
              "humanModesFor shows RTTY, SSB - no composites for a person to read");
    }

    std::printf("\nspot_need_test: %d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
