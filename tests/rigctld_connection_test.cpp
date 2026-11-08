// RigctldConnectionTest against fake rigctlds: does "Test connection" (#15)
// say what the radio answered, and name the broken link when it can't?
//
// ⭐ The failure cases are the point, above all RPRT -5: rigctld up, the
// connection fine, the radio silent behind it. That is a wrong baud or model,
// and today it looks exactly like success.
//
// ⛔ Also pinned: the test is READ-ONLY. Every run records the commands the
// fake received, and anything other than f, m and \get_info fails the test.
//
// ⚠ Antivirus may flag this binary: it opens listening sockets and connects to
// them. See tests/tci_discovery_test.cpp for the full note.

#include "RigctldConnectionTest.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>
#include <optional>

using namespace ShackBook;
using Outcome = RigctldTestResult::Outcome;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

enum class Behaviour {
    Normal,          // f, m (two lines), \get_info
    ModeUnsupported, // m -> RPRT -11, like a radio that cannot report mode
    NoInfo,          // \get_info -> RPRT -11
    RadioTimeout,    // f -> RPRT -5: rigctld up, radio not answering
    SerialIo,        // f -> RPRT -6: rigctld cannot use the port
    OtherError,      // f -> RPRT -8: protocol error
    Garbage,         // noise instead of a frequency
    Silent,          // accept, never answer
    Drop,            // close as soon as asked anything
};

// A fake rigctld speaking the same subset as rigctld_client_test's FakeRig.
class FakeRig : public QObject {
public:
    explicit FakeRig(Behaviour b, QObject* parent = nullptr)
        : QObject(parent), m_behaviour(b)
    {
        m_server = new QTcpServer(this);
        m_listening = m_server->listen(QHostAddress::LocalHost, 0);
        connect(m_server, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket* s = m_server->nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s]() {
                while (s->canReadLine()) {
                    const QString cmd = QString::fromUtf8(s->readLine()).trimmed();
                    commands << cmd;
                    reply(s, cmd);
                }
            });
        });
    }

    bool    listening() const { return m_listening; }
    quint16 port()      const { return m_server->serverPort(); }
    QStringList commands;

private:
    void reply(QTcpSocket* s, const QString& cmd)
    {
        switch (m_behaviour) {
        case Behaviour::Silent:       return;
        case Behaviour::Drop:         s->disconnectFromHost(); return;
        case Behaviour::Garbage:      s->write("\x01\x02 not-a-frequency ???\n"); return;
        case Behaviour::RadioTimeout: s->write("RPRT -5\n"); return;
        case Behaviour::SerialIo:     s->write("RPRT -6\n"); return;
        case Behaviour::OtherError:   s->write("RPRT -8\n"); return;
        default: break;
        }
        if (cmd == QLatin1String("f")) {
            s->write("435645935\n");
        } else if (cmd == QLatin1String("m")) {
            if (m_behaviour == Behaviour::ModeUnsupported) s->write("RPRT -11\n");
            else s->write("FM\n15000\n");
        } else if (cmd == QLatin1String("\\get_info")) {
            if (m_behaviour == Behaviour::NoInfo) s->write("RPRT -11\n");
            else s->write("Info: IC-9700\n");
        } else {
            s->write("RPRT -11\n");
        }
    }

    QTcpServer* m_server{nullptr};
    Behaviour   m_behaviour;
    bool        m_listening{false};
};

// Run one test to completion against host:port and return the result.
std::optional<RigctldTestResult> runTest(const QString& host, quint16 port,
                                         int replyMs = 800, int maxMs = 10000)
{
    RigctldConnectionTest t;
    t.setTimeouts(5000, replyMs);   // Windows takes ~2 s to report a refused port
    std::optional<RigctldTestResult> got;
    QEventLoop loop;
    QObject::connect(&t, &RigctldConnectionTest::finished, &loop,
                     [&](const RigctldTestResult& r) { got = r; loop.quit(); });
    QTimer::singleShot(maxMs, &loop, &QEventLoop::quit);
    t.start(host, port);
    loop.exec();
    return got;
}

bool onlyReadCommands(const QStringList& cmds)
{
    for (const QString& c : cmds)
        if (c != QLatin1String("f") && c != QLatin1String("m")
            && c != QLatin1String("\\get_info"))
            return false;
    return true;
}

std::optional<RigctldTestResult> runAgainst(Behaviour b, QStringList* sent = nullptr)
{
    FakeRig rig(b);
    if (!rig.listening()) return std::nullopt;
    auto r = runTest(QStringLiteral("127.0.0.1"), rig.port());
    if (sent) *sent = rig.commands;
    return r;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ── The radio answers: the line the issue asked for ─────────────────
    {
        QStringList sent;
        const auto r = runAgainst(Behaviour::Normal, &sent);
        check(r.has_value(), "a working rigctld finishes the test");
        if (r) {
            check(r->outcome == Outcome::Ok, "a working radio is reported OK");
            check(qAbs(r->freqMhz - 435.645935) < 1e-9, "the frequency is what the radio said");
            check(r->mode == QStringLiteral("FM"), "the mode is what the radio said");
            check(r->model == QStringLiteral("IC-9700"), "the model comes from \\get_info");
            const QString text = describeRigctldTest(*r);
            check(text.contains(QStringLiteral("IC-9700 reports 435.645935 MHz, FM")),
                  "the message says what the radio reported");
            check(text.contains(QStringLiteral("127.0.0.1:")), "the message names the address tested");
        }
        check(sent == QStringList({QStringLiteral("f"), QStringLiteral("m"),
                                   QStringLiteral("\\get_info")}),
              "it asks f, then m, then \\get_info, once each");
    }

    // ── Optional extras missing: still a pass, because f proved the link ─
    {
        const auto r = runAgainst(Behaviour::ModeUnsupported);
        check(r && r->outcome == Outcome::Ok && r->mode.isEmpty(),
              "a radio that can't report mode still passes");
        if (r) check(describeRigctldTest(*r).contains(QStringLiteral("mode not reported")),
                     "and the message says the mode wasn't reported");
    }
    {
        const auto r = runAgainst(Behaviour::NoInfo);
        check(r && r->outcome == Outcome::Ok && r->model.isEmpty(),
              "no \\get_info still passes");
        if (r) check(describeRigctldTest(*r).contains(QStringLiteral(": radio reports")),
                     "and falls back to 'radio'");
    }

    // ⭐ ── rigctld up, radio silent behind it: the case that looked like success ─
    {
        const auto r = runAgainst(Behaviour::RadioTimeout);
        check(r && r->outcome == Outcome::RadioNotAnswering,
              "RPRT -5 is reported as the radio not answering");
        if (r) {
            check(r->rprt == -5, "the Hamlib code is kept");
            check(describeRigctldTest(*r).contains(QStringLiteral("baud rate")),
                  "and the message points at the baud rate");
        }
    }
    {
        const auto r = runAgainst(Behaviour::SerialIo);
        check(r && r->outcome == Outcome::SerialError, "RPRT -6 is reported as a serial-port problem");
    }
    {
        const auto r = runAgainst(Behaviour::OtherError);
        check(r && r->outcome == Outcome::NotUnderstood && r->rprt == -8,
              "another RPRT error is 'not understood', with its code");
        if (r) check(describeRigctldTest(*r).contains(QStringLiteral("error -8")),
                     "and the message carries the code");
    }
    {
        const auto r = runAgainst(Behaviour::Garbage);
        check(r && r->outcome == Outcome::NotUnderstood && r->freqMhz == 0.0,
              "noise is never believed as a frequency");
    }

    // ── Nothing answering at all ────────────────────────────────────────
    {
        const auto r = runAgainst(Behaviour::Silent);
        check(r && r->outcome == Outcome::RigctldSilent,
              "a connection that never answers is 'rigctld silent', not a hang");
    }
    {
        const auto r = runAgainst(Behaviour::Drop);
        check(r && r->outcome == Outcome::Dropped, "rigctld closing mid-test is reported");
    }
    {
        // A port that was listening a moment ago and isn't now.
        quint16 deadPort = 0;
        {
            QTcpServer probe;
            probe.listen(QHostAddress::LocalHost, 0);
            deadPort = probe.serverPort();
        }
        const auto r = runTest(QStringLiteral("127.0.0.1"), deadPort);
        check(r && r->outcome == Outcome::NotListening, "a closed port is 'nothing listening'");
        if (r) check(describeRigctldTest(*r).contains(QStringLiteral("rigctld doesn't appear to be running")),
                     "and the message says rigctld isn't running there");
    }

    // ⛔ ── Read-only, whatever the radio does ─────────────────────────────
    {
        bool allRead = true;
        for (Behaviour b : {Behaviour::Normal, Behaviour::ModeUnsupported, Behaviour::NoInfo,
                            Behaviour::RadioTimeout, Behaviour::SerialIo, Behaviour::OtherError,
                            Behaviour::Garbage, Behaviour::Silent}) {
            QStringList sent;
            runAgainst(b, &sent);
            if (!onlyReadCommands(sent)) allRead = false;
        }
        check(allRead, "only f, m and \\get_info are ever sent");
    }

    // ── Wording, without a network ──────────────────────────────────────
    {
        RigctldTestResult r;
        r.host = QStringLiteral("shack-pi");
        r.port = 4533;
        r.outcome = Outcome::HostNotFound;
        check(describeRigctldTest(r).contains(QStringLiteral("\"shack-pi\"")),
              "an unknown host is named in the message");
        r.outcome = Outcome::NoAnswer;
        check(describeRigctldTest(r).contains(QStringLiteral("shack-pi:4533")),
              "a timeout names host:port");
    }

    if (failures == 0) {
        std::printf("\nrigctld_connection_test: all checks passed\n");
        return 0;
    }
    std::printf("\nrigctld_connection_test: %d check(s) FAILED\n", failures);
    return 1;
}
