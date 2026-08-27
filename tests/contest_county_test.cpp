// contest_county_test — contest definitions and the worked/needed table (#4).
//
// Two things under test, both of which fail in quiet ways if wrong:
//
//   ContestCatalog — a malformed definition must be SKIPPED, not half-loaded.
//   A definition missing one exchange field would lay out an entry form that
//   silently cannot capture part of the exchange.
//
//   countyProgress — the table must be seeded from the county LIST, not from
//   the log. A table built only from worked QSOs can never show what is
//   needed, which is the entire point of the feature.

#include "ContestDef.h"
#include "CountyList.h"
#include "CountyProgress.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cstdio>

using namespace ShackLog;

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { ++g_failures; std::printf("FAIL: %s\n", what); }
    else       std::printf("[ OK ] %s\n", what);
}

static Qso qso(const char* cnty, const char* state = "MD",
               const char* lotw = "", const char* qsl = "")
{
    Qso q;
    q.cnty     = QString::fromLatin1(cnty);
    q.state    = QString::fromLatin1(state);
    q.lotwRcvd = QString::fromLatin1(lotw);
    q.qslRcvd  = QString::fromLatin1(qsl);
    q.call     = QStringLiteral("W1AW");
    return q;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ── ContestCatalog ─────────────────────────────────────────────────
    ContestCatalog cat;
    const int n = cat.load(QStringLiteral(SHACKLOG_CONTESTS_DAT));
    std::printf("loaded %d contest definitions\n", n);
    check(n > 0, "the bundled contest definitions load");

    const ContestDef mdc = cat.find("MDC-QSO-PARTY");
    check(mdc.isValid(),                  "MDC-QSO-PARTY is defined");
    check(mdc.countyState == "MD",        "and is a Maryland county party");
    check(mdc.isCountyParty(),            "isCountyParty() agrees");
    check(cat.find("mdc-qso-party").isValid(), "lookup is case-insensitive");

    // ⭐ An unknown contest must return an INVALID def, not an empty-but-valid
    // one: callers switch on isValid() to fall back to the generic layout,
    // and a valid def with no exchange would lay out an empty form.
    const ContestDef unknown = cat.find("CQ-WW-SSB");
    check(!unknown.isValid(), "an undefined contest yields an INVALID def");
    check(!cat.find("").isValid(), "an empty contest id yields an invalid def");

    check(!cat.countyParties().isEmpty(), "county parties can be listed");

    // ── ⭐ Provenance: every bundled row states whether its exchange was
    // checked against the sponsor's rules. A definition that is present but
    // WRONG is worse than one that is absent, so this must be readable by a
    // caller rather than living only in a comment.
    check(!mdc.exchangeConfirmed,
          "the bundled definitions are honestly marked UNCONFIRMED");
    for (const ContestDef& d : cat.all())
        if (d.exchangeConfirmed) {
            check(false, "a row claims CONFIRMED — has its exchange really been checked?");
            break;
        }

    // A row whose Status is missing or unrecognised is REJECTED, not
    // defaulted. Whichever default were picked would be a guess about the
    // provenance of somebody else's edit.
    {
        const QString tmp = QDir::tempPath() + QStringLiteral("/shacklog_contest_status_test.dat");
        QFile f(tmp);
        check(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text),
              "can write a temporary definitions file");
        {
            QTextStream out(&f);
            out << "GOOD|Good Party|RST,COUNTY|RST,COUNTY|MD|0|UNCONFIRMED\n";
            out << "SIXCOL|Missing Status|RST,COUNTY|RST,COUNTY|MD|0\n";
            out << "BADSTAT|Bad Status|RST,COUNTY|RST,COUNTY|MD|0|PROBABLY\n";
            out << "OKCONF|Confirmed Party|RST,COUNTY|RST,COUNTY|MD|0|CONFIRMED\n";
        }
        f.close();

        ContestCatalog c2;
        const int loaded = c2.load(tmp);
        check(loaded == 2, "only the two rows with a valid Status load");
        check(c2.find("GOOD").isValid(),    "an UNCONFIRMED row loads");
        check(!c2.find("SIXCOL").isValid(), "a row MISSING the Status field is rejected");
        check(!c2.find("BADSTAT").isValid(),"a row with an unrecognised Status is rejected");
        check(c2.find("OKCONF").exchangeConfirmed,
              "CONFIRMED is carried through to the definition");
        QFile::remove(tmp);
    }
    for (const ContestDef& d : cat.countyParties())
        if (!d.isCountyParty()) { check(false, "countyParties() returned a non-party"); break; }

    // Exchange order is a UI contract — it sets tab order in the entry form.
    check(mdc.sent.size() >= 2 && mdc.sent.first() == ExchangeField::Rst,
          "the exchange starts with RST, in sending order");
    const ContestDef cqp = cat.find("CQP");
    check(cqp.isValid() && cqp.sent.contains(ExchangeField::Serial),
          "a party with a serial records it in the exchange");

    // Field names round-trip, so the data file and the code cannot drift.
    bool ok = false;
    check(ContestCatalog::fieldFromName("COUNTY", &ok) == ExchangeField::County && ok,
          "a field name parses");
    check(ContestCatalog::fieldName(ExchangeField::County) == "COUNTY",
          "and renders back to the same name");
    ContestCatalog::fieldFromName("NOT-A-FIELD", &ok);
    check(!ok, "an unknown field name reports failure rather than guessing");

    // ── countyProgress ─────────────────────────────────────────────────
    CountyList counties;
    const int cn = counties.load(QStringLiteral(SHACKLOG_COUNTIES_DAT));
    check(cn > 3000, "the county list loads");
    if (cn == 0) { std::fprintf(stderr, "cannot continue\n"); return 1; }

    {
        // ⭐ THE POINT OF THE FEATURE: an empty log still lists every county,
        // all needed. A table built from the log alone could never do this.
        const auto p = countyProgress(counties, "MD", {});
        check(p.total == 24,  "Maryland seeds all 24 counties from the LIST");
        check(p.worked == 0,  "none worked on an empty log");
        check(p.needed() == 24, "so all 24 are needed");
        check(p.counties.size() == 24, "and every one is present in the table");
    }
    {
        QVector<Qso> v{qso("MD,Anne Arundel"), qso("MD,Calvert"), qso("MD,Anne Arundel")};
        const auto p = countyProgress(counties, "MD", v);
        check(p.worked == 2,   "two DISTINCT counties worked, not three QSOs");
        check(p.needed() == 22, "and the rest still needed");
        check(p.confirmed == 0, "nothing confirmed without LoTW or a card");
    }
    {
        // Confirmed counts LoTW or a card, matching awardsSummary().
        QVector<Qso> v{qso("MD,Calvert", "MD", "Y"), qso("MD,Howard", "MD", "", "Y"),
                       qso("MD,Carroll")};
        const auto p = countyProgress(counties, "MD", v);
        check(p.worked == 3,    "three counties worked");
        check(p.confirmed == 2, "two confirmed - LoTW or card, not the third");
    }
    {
        // Bare county name resolved from the QSO's own state column.
        QVector<Qso> v{qso("Anne Arundel", "MD")};
        const auto p = countyProgress(counties, "MD", v);
        check(p.worked == 1, "a bare county name resolves via the state column");
    }
    {
        // ⭐ A real county logged against the WRONG state is reported, not
        // counted. These are actual strings from the reference logbook.
        QVector<Qso> v{qso("MD,Calvert"), qso("MD,Nonesuch")};
        const auto p = countyProgress(counties, "MD", v);
        check(p.worked == 1, "the good QSO counts");
        check(p.unmatched.size() == 1, "and the bad one is REPORTED");
        check(p.unmatched.first().first == "MD,Nonesuch", "naming the offending value");
    }
    {
        // An out-of-state QSO is not this party's business and must NOT be
        // reported as a problem, or every DX contact becomes a false alarm.
        QVector<Qso> v{qso("VA,Fairfax", "VA"), qso("MD,Calvert")};
        const auto p = countyProgress(counties, "MD", v);
        check(p.worked == 1,          "only the in-state QSO counts toward MD");
        check(p.unmatched.isEmpty(),  "an out-of-state county is NOT flagged as unmatched");
    }
    {
        // ⭐ Baltimore County and Baltimore city are scored separately, which
        // is why CountyList keeps them distinct (#5).
        QVector<Qso> v{qso("MD,Baltimore County"), qso("MD,Baltimore city")};
        const auto p = countyProgress(counties, "MD", v);
        check(p.worked == 2, "Baltimore County and Baltimore city count SEPARATELY");
    }
    {
        const auto p = countyProgress(counties, "ZZ", {});
        check(p.total == 0 && p.counties.isEmpty(), "an unknown state yields nothing");
    }

    if (g_failures == 0) {
        std::printf("contest_county_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "contest_county_test: %d failure(s)\n", g_failures);
    return 1;
}
