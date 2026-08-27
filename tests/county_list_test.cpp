// county_list_test — the county reference list and its name matcher (#5).
//
// The cases here are MEASURED, not invented: every "real log" string below was
// taken from the 7379 QSOs carrying a cnty value in the reference logbook, and
// the collision cases are the six name pairs the US Census actually ships.
// A matcher for this data fails in specific ways, and each is pinned:
//
//   - a county and an independent city sharing a name (Baltimore, Fairfax,
//     Franklin, Richmond, Roanoke, St. Louis) must stay DISTINCT
//   - "Island" is administrative for the USVI districts but part of the name
//     for Island County WA and Rock Island County IL
//   - a bare county name is only resolvable with the QSO's own state column
//   - a real county logged against the WRONG state must NOT match

#include "CountyList.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <cstdio>

using ShackLog::CountyList;

static int g_failures = 0;

static void check(bool ok, const QString& what)
{
    if (!ok) { ++g_failures; std::printf("FAIL: %s\n", qPrintable(what)); }
    else       std::printf("[ OK ] %s\n", qPrintable(what));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    CountyList cl;
    // The test runs against the source tree's data file rather than the Qt
    // resource, so it does not need the app's .qrc to be linked in.
    const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral(SHACKLOG_COUNTIES_DAT);
    const int n = cl.load(path);
    std::printf("loaded %d counties from %s\n", n, qPrintable(path));
    check(n > 3000, "the bundled list has the ~3235 US counties");
    if (n == 0) { std::fprintf(stderr, "cannot continue without data\n"); return 1; }

    // ── Canonical ADIF form, the 88% case ──────────────────────────────
    check(cl.lookup("MD,Anne Arundel").fips == "24003", "MD,Anne Arundel resolves");
    check(cl.lookup("CT,Hartford").state == "CT",       "CT,Hartford resolves");
    check(cl.lookup("NY,Dutchess").name.startsWith("Dutchess"), "NY,Dutchess resolves");

    // ── Administrative words the operator does not type ────────────────
    check(cl.lookup("LA,Ascension Parish").fips == cl.lookup("LA,Ascension").fips,
          "a Parish suffix matches the same county as the bare name");
    check(cl.lookup("AK,Kenai Peninsula Borough").fips == cl.lookup("AK,Kenai Peninsula").fips,
          "a Borough suffix matches the same borough as the bare name");
    check(cl.lookup("PR,San Juan").fips == "72127", "a PR Municipio resolves from the bare name");

    // ── Saint / St. / Ste. folding, all three present in real logs ─────
    check(cl.lookup("LA,St. Tammany").fips == cl.lookup("LA,Saint Tammany").fips,
          "St. and Saint fold to the same county");
    check(cl.lookup("LA,St Mary Parish").fips == cl.lookup("LA,St. Mary").fips,
          "St and St. fold together with a Parish suffix");

    // ── ⭐ The collision cases: a county and an independent city ────────
    // Stripping the administrative word would merge these into one entry and
    // silently halve the county count for the state.
    const auto balCounty = cl.lookup("MD,Baltimore County");
    const auto balCity   = cl.lookup("MD,Baltimore city");
    check(!balCounty.fips.isEmpty() && !balCity.fips.isEmpty(),
          "both Baltimore County and Baltimore city resolve");
    check(balCounty.fips != balCity.fips,
          "Baltimore County and Baltimore city are DISTINCT entities");
    check(cl.lookup("VA,Fairfax County").fips != cl.lookup("VA,Fairfax city").fips,
          "Fairfax County and Fairfax city are distinct");
    check(cl.lookup("MO,St. Louis County").fips != cl.lookup("MO,St. Louis city").fips,
          "St. Louis County and St. Louis city are distinct");

    // ── ⭐ "Island": administrative for USVI, part of the name elsewhere ─
    check(cl.lookup("VI,Saint Croix").fips == "78010",
          "VI,Saint Croix resolves to Census 'St. Croix Island' (71 real QSOs)");
    check(!cl.lookup("WA,Island").fips.isEmpty(),
          "Island County WA still resolves — Island is its NAME, not a suffix");
    check(!cl.lookup("IL,Rock Island").fips.isEmpty(),
          "Rock Island County IL still resolves");

    // ── Bare names, recoverable from the QSO's own state column ────────
    check(cl.lookup("Anne Arundel", "MD").fips == "24003",
          "a bare county name resolves when the QSO carries its state");
    check(cl.lookup("Anne Arundel").fips.isEmpty(),
          "a bare county name with NO state does not guess");
    // ⚠ These three pin BEHAVIOUR, not the early-out that implements it.
    // Removing the explicit empty-state guard in lookup() was mutation-tested
    // and changed nothing observable: an empty state simply builds a key like
    // "|ANNE ARUNDEL" that cannot exist. Worth stating so nobody adds a test
    // "for the guard" and believes it is defending something.
    check(cl.lookup(",Anne Arundel").fips.isEmpty(),
          "an empty state before the comma does not resolve");
    check(cl.lookup("Anne Arundel", "   ").fips.isEmpty(),
          "a whitespace-only state column does not resolve");

    // ── ⭐ Wrong-state values must be REJECTED, not silently matched ────
    // These are real strings from the log: Kane is IL/UT, Calvert is MD,
    // Suffolk is MA/NY. Matching them would turn a logging error into a
    // wrong award count, which is what the bogus set exists to prevent.
    check(cl.lookup("AZ,Kane").fips.isEmpty(),    "AZ,Kane is rejected (Kane is IL/UT)");
    check(cl.lookup("SC,Calvert").fips.isEmpty(), "SC,Calvert is rejected (Calvert is MD)");
    check(cl.lookup("SC,Suffolk").fips.isEmpty(), "SC,Suffolk is rejected (Suffolk is MA/NY)");
    check(!cl.lookup("UT,Kane").fips.isEmpty(),   "UT,Kane DOES resolve — the rejection is state-specific");

    // ── Non-US noise present in real logs must not resolve ─────────────
    check(cl.lookup("Toronto", "ON").fips.isEmpty(), "a Canadian town does not resolve");
    check(cl.lookup("Germany").fips.isEmpty(),       "a bare country name does not resolve");

    // ── Per-state enumeration, what a worked/needed table needs ────────
    check(cl.countiesIn("MD").size() == 24, "Maryland has 24 counties (23 + Baltimore city)");
    check(cl.countiesIn("TX").size() == 254, "Texas has 254 counties");
    check(cl.countiesIn("DE").size() == 3,   "Delaware has 3 counties");
    check(cl.countiesIn("ZZ").isEmpty(),     "an unknown state yields nothing");
    check(cl.countiesIn("md").size() == 24,  "the state code is case-insensitive");

    // ── Normalisation is a pure function, usable without a loaded list ──
    check(CountyList::normalize("  anne   arundel  county ") == "ANNE ARUNDEL",
          "normalize collapses case, spacing and the County suffix");
    check(CountyList::normalize("Prince George's") == "PRINCE GEORGES",
          "normalize strips an apostrophe");
    check(CountyList::normalize("Miami-Dade") == "MIAMI DADE",
          "normalize turns a hyphen into a space");

    if (g_failures == 0) {
        std::printf("county_list_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "county_list_test: %d failure(s)\n", g_failures);
    return 1;
}
