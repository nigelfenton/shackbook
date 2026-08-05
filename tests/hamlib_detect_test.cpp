// Finding Hamlib's rigctld — and being honest when it is not there.
//
// ShackLog requires Hamlib but does not ship it, which is only a defensible
// decision if the missing case is handled properly. The person without Hamlib
// is exactly the person least able to work out why nothing happens: "not
// installed", "radio switched off", "wrong port" and "firewall" all end as
// no-connection, and only the first is fixed by installing something.
//
// So detection must be RIGHT rather than optimistic — a false positive here
// produces the worst outcome, an operator told the tool is present while it
// silently is not.

#include "RigctldClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
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

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::printf("FAIL  could not create temp dir\n");
        return 1;
    }

    // A stand-in for an installed rigctld. Contents are irrelevant —
    // detection is about existence, not about running anything.
    const QString fakePath = tmp.filePath(
#ifdef Q_OS_WIN
        QStringLiteral("rigctld.exe")
#else
        QStringLiteral("rigctld")
#endif
    );
    {
        QFile f(fakePath);
        check(f.open(QIODevice::WriteOnly), "created a stand-in rigctld");
        f.write("not a real binary\n");
        f.close();
    }

    // ── A configured path wins ──────────────────────────────────────────
    check(RigctldClient::findRigctld(fakePath) == fakePath,
          "an explicitly configured path is used as given");

    check(RigctldClient::findRigctld(QStringLiteral("  ") + fakePath + QStringLiteral("  "))
              == fakePath,
          "a configured path is trimmed before use");

    // ── ⛔ A configured path that does not exist must FAIL, not fall back ──
    // Falling back to PATH here would be the dangerous kindness: the operator
    // typed a specific location, so silently using a different rigctld would
    // hide their typo and connect to something they did not choose.
    const QString bogus = tmp.filePath(QStringLiteral("nope/rigctld-does-not-exist"));
    check(RigctldClient::findRigctld(bogus).isEmpty(),
          "a configured path that does not exist reports NOT FOUND, never falls back");

    // ── No configuration: search the usual places, then PATH ────────────
    // On this machine the answer may legitimately be either — Hamlib is not
    // installed on the dev box, but a CI runner or a ham's PC may well have it.
    // Both outcomes are correct; what must hold is that a REPORTED path exists.
    const QString auto1 = RigctldClient::findRigctld();
    if (auto1.isEmpty()) {
        std::printf("      (no system rigctld here — the not-installed path is live)\n");
        check(true, "auto-detect reports empty rather than guessing a path");
    } else {
        check(QFileInfo::exists(auto1),
              "auto-detect only ever reports a path that actually exists");
    }

    // Whatever the answer, it must be stable — a detector that flip-flops
    // would produce guidance that changes for no reason.
    check(RigctldClient::findRigctld() == auto1, "auto-detect is stable across calls");

    // ── An empty configured path means "look for it", not "fail" ────────
    check(RigctldClient::findRigctld(QString{}) == auto1,
          "an empty configured path falls through to auto-detect");
    check(RigctldClient::findRigctld(QStringLiteral("   ")) == auto1,
          "a whitespace-only configured path is treated as empty");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
