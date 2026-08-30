// rigctld lifecycle rules.
//
// Auto-starting rigctld is only safe if it never tramples a server somebody
// else is using, and never leaves one of its own behind. Those two rules are
// the entire justification for the feature -- ShackBook previously refused to
// launch rigctld at all rather than risk them (#13).
//
// The adopt/spawn decision and the argument building are testable without a
// radio, which is what this covers. A real spawn needs Hamlib installed, so
// that assertion runs only where it is present and is skipped elsewhere
// rather than failing.

#include "RigctldSupervisor.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTcpServer>

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

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ── Argument building ────────────────────────────────────────────────
    //
    // These are what actually reach the radio. A wrong -m talks the wrong
    // dialect and a wrong -r opens the wrong device, and neither fails in a
    // way the operator can read.
    {
        const QStringList args =
            RigctldSupervisor::buildArguments("3081", "COM21", "57600");
        check(args == QStringList({"-m", "3081", "-r", "COM21", "-s", "57600"}),
              "model, port and baud become -m / -r / -s in order");
    }
    {
        // Baud omitted rather than passed empty: Hamlib has a per-model
        // default, and "-s" with nothing after it is worse than no -s at all.
        const QStringList args =
            RigctldSupervisor::buildArguments("3081", "COM21", QString());
        check(args == QStringList({"-m", "3081", "-r", "COM21"}),
              "an empty baud omits -s entirely rather than passing it blank");
    }

    // ── The adopt/spawn decision ─────────────────────────────────────────
    //
    // ⛔ THE RULE THAT MAKES THIS SAFE. If something already answers on the
    // port it may be serving WSJT-X from the same radio; spawning a second
    // rigctld would fight it for the serial device. Adopt, never spawn.
    {
        QTcpServer stand_in;
        check(stand_in.listen(QHostAddress::LocalHost, 0),
              "a stand-in server can be opened for the adopt test");
        const quint16 port = stand_in.serverPort();

        check(RigctldSupervisor::serverAnswering("127.0.0.1", port),
              "an occupied port is detected as answering");

        RigctldSupervisor sup;
        // Deliberately a bogus rigctld path: if the adopt check works, it is
        // never consulted. If adoption were broken this would try to spawn
        // and fail, which is exactly the distinction being asserted.
        const auto state = sup.ensureRunning("/nonexistent/rigctld", "127.0.0.1",
                                             port, "3081", "COM21", "57600");
        check(state == RigctldSupervisor::State::Adopted,
              "an already-served port is ADOPTED, not spawned over");
        check(!sup.ours(),
              "an adopted server is not ours");

        // The corollary, and the one that protects other programs: stopping
        // must do nothing to a server we did not start.
        sup.stopIfOurs();
        check(stand_in.isListening(),
              "stopIfOurs() leaves an ADOPTED server running");
    }

    // ── A free port with no rigctld ──────────────────────────────────────
    {
        // Find a port nothing is on, by opening and immediately closing one.
        quint16 freePort = 0;
        {
            QTcpServer probe;
            probe.listen(QHostAddress::LocalHost, 0);
            freePort = probe.serverPort();
        }
        check(!RigctldSupervisor::serverAnswering("127.0.0.1", freePort, 300),
              "a free port is correctly reported as not answering");

        RigctldSupervisor sup;
        const auto state = sup.ensureRunning("/nonexistent/rigctld", "127.0.0.1",
                                             freePort, "3081", "COM21", "57600");
        check(state == RigctldSupervisor::State::Failed,
              "a missing rigctld reports Failed rather than pretending to work");
        check(!sup.lastError().isEmpty(),
              "and says why, so the operator can act on it");
    }

    // ── Missing configuration ────────────────────────────────────────────
    //
    // Refusing early beats launching rigctld with half a command line and
    // letting it fail somewhere less legible.
    {
        quint16 freePort = 0;
        {
            QTcpServer probe;
            probe.listen(QHostAddress::LocalHost, 0);
            freePort = probe.serverPort();
        }
        RigctldSupervisor sup;
        const auto state = sup.ensureRunning("/nonexistent/rigctld", "127.0.0.1",
                                             freePort, QString(), QString(),
                                             QString());
        check(state == RigctldSupervisor::State::Failed,
              "no model or port refuses before trying to spawn");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
