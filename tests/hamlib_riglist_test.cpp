// Parsing the Hamlib rig catalogue.
//
// The picker's whole value is that the operator no longer has to find "3081"
// among 314 rigs by hand. That only holds if the table is parsed correctly,
// and the table is fixed-column text with spaces INSIDE the fields -- which
// is exactly the kind of thing that looks right until a manufacturer with a
// space in its name shifts every column.
//
// Deliberately parses a captured fixture rather than running rigctl: the
// parser must be testable on a machine with no Hamlib installed.

#include "HamlibRigList.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++failures;
}

// Real output from `rigctl -l`, Hamlib 4.7.2 on Windows, trimmed to the rows
// that matter. Kept verbatim -- retyping it would lose the column widths that
// are the point of the test.
const char* kRigctlOutput =
    " Rig #  Mfg                    Model                   Version         Status      Macro\n"
    "     1  Hamlib                 Dummy                   20240709.0      Stable      RIG_MODEL_DUMMY\n"
    "     2  Hamlib                 NET rigctl              20250211.0      Stable      RIG_MODEL_NETRIGCTL\n"
    "  1032  Yaesu                  FTDX-5000               20241118.11     Stable      RIG_MODEL_FTDX5000\n"
    "  1033  Vertex Standard        VX-1700                 20210221.0      Stable      RIG_MODEL_VX1700\n"
    "  3081  Icom                   IC-9700                 20250517.20     Stable      RIG_MODEL_IC9700\n";

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QList<HamlibRigList::Rig> rigs =
        HamlibRigList::parseRigctlList(QString::fromUtf8(kRigctlOutput));

    // Five data rows, not six: the header must not become a rig. Its first
    // column is "Rig #", which is not a number -- that is what excludes it.
    check(rigs.size() == 5, "the header row is not parsed as a rig");

    // The case this whole feature exists for.
    bool found9700 = false;
    for (const HamlibRigList::Rig& r : rigs) {
        if (r.model == 3081) {
            found9700 = true;
            check(r.manufacturer == QLatin1String("Icom")
                      && r.name == QLatin1String("IC-9700"),
                  "the IC-9700 parses as Icom / IC-9700 / 3081");
        }
    }
    check(found9700, "the IC-9700 is found by its model number");

    // ⛔ THE COLUMN TRAP. "Vertex Standard" and "NET rigctl" contain single
    // spaces. A parser that split on one space would read the manufacturer as
    // "Vertex" and shift the model to "Standard" -- plausible-looking and
    // completely wrong. These two rows are here to catch exactly that.
    bool checkedVertex = false;
    bool checkedNet = false;
    for (const HamlibRigList::Rig& r : rigs) {
        if (r.model == 1033) {
            checkedVertex = true;
            check(r.manufacturer == QLatin1String("Vertex Standard")
                      && r.name == QLatin1String("VX-1700"),
                  "a manufacturer containing a space is not split in half");
        }
        if (r.model == 2) {
            checkedNet = true;
            check(r.name == QLatin1String("NET rigctl"),
                  "a model name containing a space survives intact");
        }
    }
    check(checkedVertex && checkedNet,
          "both space-containing fixtures were actually exercised");

    // The label is what the operator searches, so the number has to be in it:
    // someone who knows they want 3081 must be able to type it.
    for (const HamlibRigList::Rig& r : rigs) {
        if (r.model == 3081) {
            check(r.label() == QLatin1String("Icom IC-9700 (3081)"),
                  "the label carries manufacturer, model AND number");
        }
    }

    // Junk must be ignored rather than becoming half a rig. A warning printed
    // to the same stream is the realistic case.
    const QList<HamlibRigList::Rig> noise = HamlibRigList::parseRigctlList(
        QStringLiteral("rigctl: some warning\n\n  not a table at all\n"));
    check(noise.isEmpty(), "non-table output yields no rigs, not garbage ones");

    check(HamlibRigList::parseRigctlList(QString{}).isEmpty(),
          "empty output yields no rigs");

    // The fallback exists so the picker is useful before Hamlib is installed.
    // It is worthless if it does not contain a plausible radio with a real
    // number, so assert the one this shack actually uses.
    const QList<HamlibRigList::Rig> fallback = HamlibRigList::bundledFallback();
    check(!fallback.isEmpty(), "the bundled fallback is not empty");
    bool fallbackHas9700 = false;
    for (const HamlibRigList::Rig& r : fallback)
        if (r.model == 3081 && r.name == QLatin1String("IC-9700"))
            fallbackHas9700 = true;
    check(fallbackHas9700, "the fallback carries the IC-9700 with its real number");

    // A path that is not Hamlib must yield nothing rather than inventing rigs.
    check(HamlibRigList::fromInstalledHamlib(QString{}).isEmpty(),
          "an empty rigctld path reports no rigs");
    check(HamlibRigList::fromInstalledHamlib(QStringLiteral("/nonexistent/rigctld")).isEmpty(),
          "a bogus rigctld path reports no rigs rather than failing");

    // best() must fall back rather than return nothing when Hamlib is absent.
    bool usedFallback = false;
    const QList<HamlibRigList::Rig> best =
        HamlibRigList::best(QStringLiteral("/nonexistent/rigctld"), &usedFallback);
    check(!best.isEmpty() && usedFallback,
          "with no Hamlib, best() returns the fallback AND says that it did");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
