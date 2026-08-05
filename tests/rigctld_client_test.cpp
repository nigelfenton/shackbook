// RigctldClient against fake radios: does it follow a radio, and does it
// refuse to believe one that is lying or silent?
//
// The fakes are built here rather than shelling out to the Python personas, so
// the test is self-contained and runs anywhere. They speak the same subset:
//   f            -> frequency in Hz
//   m            -> mode, then passband, on TWO lines
//   \get_info    -> "Info: <model>"
//   anything     -> RPRT -11
//
// ⭐ The failure cases are the point. A radio that answers correctly proves
// only the happy path; these pin what happens when the CAT link is garbled
// (RF desense at Field Day), silent (wedged adaptor), or drops (cable out).
//
// ⚠ Antivirus may flag this binary — it opens listening sockets and connects to
// them, which is behaviourally a port scanner. If it passes standalone and
// fails under ctest, that is the scanner, not the code. See
// tests/tci_discovery_test.cpp for the full note.

#include "RigctldClient.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>

using namespace ShackLog;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

// Spin the event loop for a bounded time, or until a predicate goes true.
template <typename Pred>
void waitFor(Pred p, int maxMs = 4000)
{
    QEventLoop loop;
    QTimer tick;
    tick.setInterval(20);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&]() {
        if (p()) loop.quit();
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    tick.start();
    deadline.start(maxMs);
    loop.exec();
}

enum class Behaviour { Normal, Garbage, Silent, Drop };

// A fake rigctld. Mirrors rigctld_persona.py so the C++ side is tested against
// the same protocol the Giga and the Python personas speak.
class FakeRig : public QObject {
public:
    FakeRig(const QString& model, qint64 freqHz, const QString& mode,
            Behaviour b = Behaviour::Normal, QObject* parent = nullptr)
        : QObject(parent), m_model(model), m_freq(freqHz), m_mode(mode), m_behaviour(b)
    {
        m_server = new QTcpServer(this);
        m_listening = m_server->listen(QHostAddress::LocalHost, 0);
        connect(m_server, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket* s = m_server->nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s]() {
                while (s->canReadLine()) {
                    const QString cmd = QString::fromUtf8(s->readLine()).trimmed();
                    ++m_commands;
                    switch (m_behaviour) {
                    case Behaviour::Silent:
                        continue;                       // accept, never answer
                    case Behaviour::Drop:
                        s->disconnectFromHost();
                        return;
                    case Behaviour::Garbage:
                        s->write("\x01\x02 not-a-frequency ???\n");
                        continue;
                    case Behaviour::Normal:
                        break;
                    }
                    if (cmd == QLatin1String("f")) {
                        s->write(QByteArray::number(m_freq) + "\n");
                    } else if (cmd == QLatin1String("m")) {
                        s->write(m_mode.toUtf8() + "\n3000\n");
                    } else if (cmd == QLatin1String("\\get_info")) {
                        s->write("Info: " + m_model.toUtf8() + "\n");
                    } else {
                        s->write("RPRT -11\n");
                    }
                }
            });
        });
    }

    bool    listening() const { return m_listening; }
    quint16 port()      const { return m_server->serverPort(); }
    int     commands()  const { return m_commands; }
    void    setFreq(qint64 hz) { m_freq = hz; }
    void    setMode(const QString& m) { m_mode = m; }

private:
    QTcpServer* m_server{nullptr};
    QString m_model;
    qint64  m_freq;
    QString m_mode;
    Behaviour m_behaviour;
    bool m_listening{false};
    int  m_commands{0};
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ── A radio that behaves ────────────────────────────────────────────
    {
        FakeRig rig(QStringLiteral("IC-9700"), 14074000LL, QStringLiteral("USB"));
        check(rig.listening(), "fake IC-9700 is listening");

        RigctldClient client;
        client.setPollIntervalMs(100);
        client.connectToServer(QStringLiteral("127.0.0.1"), rig.port());

        waitFor([&] { return client.connected(); });
        check(client.connected(), "client connects to rigctld");

        waitFor([&] { return !client.modelName().isEmpty(); });
        check(client.modelName() == QStringLiteral("IC-9700"),
              "model name comes from \\get_info — the RADIO, not the application");

        waitFor([&] { return client.currentFrequencyMhz() > 0.0; });
        check(qAbs(client.currentFrequencyMhz() - 14.074) < 1e-9,
              "frequency is read and converted to MHz");

        waitFor([&] { return !client.currentMode().isEmpty(); });
        check(client.currentMode() == QStringLiteral("USB"), "mode is read");

        // The radio moves; the client must follow without a reconnect.
        rig.setFreq(7074000LL);
        rig.setMode(QStringLiteral("LSB"));
        waitFor([&] { return qAbs(client.currentFrequencyMhz() - 7.074) < 1e-9; });
        check(qAbs(client.currentFrequencyMhz() - 7.074) < 1e-9,
              "a frequency change is picked up by polling");
        waitFor([&] { return client.currentMode() == QStringLiteral("LSB"); });
        check(client.currentMode() == QStringLiteral("LSB"),
              "a mode change is picked up by polling");

        client.disconnectFromServer();
    }

    // ── A radio that answers noise ──────────────────────────────────────
    // The RF-desense case: the link is up, the bytes are rubbish. Storing that
    // as a frequency would put a wrong number on a real QSO.
    {
        FakeRig rig(QStringLiteral("IC-9700"), 14074000LL, QStringLiteral("USB"),
                    Behaviour::Garbage);
        RigctldClient client;
        client.setPollIntervalMs(100);
        client.connectToServer(QStringLiteral("127.0.0.1"), rig.port());
        waitFor([&] { return client.connected(); });

        // Give it plenty of chances to be fooled.
        waitFor([&] { return rig.commands() > 5; }, 3000);
        check(client.currentFrequencyMhz() == 0.0,
              "garbled replies are REJECTED — no frequency is invented");
        check(client.modelName().isEmpty(),
              "garbled replies do not become a model name");
        client.disconnectFromServer();
    }

    // ── A radio that goes silent ────────────────────────────────────────
    // Accepts commands, never answers. The client must keep asking rather than
    // wedging on the first unanswered request.
    {
        FakeRig rig(QStringLiteral("IC-9700"), 14074000LL, QStringLiteral("USB"),
                    Behaviour::Silent);
        RigctldClient client;
        client.setPollIntervalMs(100);
        client.connectToServer(QStringLiteral("127.0.0.1"), rig.port());
        waitFor([&] { return client.connected(); });

        const int early = rig.commands();
        waitFor([&] { return false; }, 3000);      // let the watchdog cycle
        const int late = rig.commands();

        check(client.currentFrequencyMhz() == 0.0,
              "a silent radio yields no frequency");
        check(late > early,
              "a silent radio does not WEDGE the client — polling continues");
        client.disconnectFromServer();
    }

    // ── A radio that drops mid-session ──────────────────────────────────
    // ⭐ The rule that matters most: the model name must be FORGOTTEN, so a QSO
    // logged after the radio goes away is left unattributed rather than
    // credited to the rig that has gone. A wrong attribution is worse than a
    // missing one — the same rule already proven for TCI.
    {
        auto* rig = new FakeRig(QStringLiteral("FT-847"), 7074000LL,
                                QStringLiteral("LSB"), Behaviour::Normal);
        RigctldClient client;
        client.setProbeMode(true);          // no reconnect; we want the drop to stick
        client.setPollIntervalMs(100);
        client.connectToServer(QStringLiteral("127.0.0.1"), rig->port());

        waitFor([&] { return !client.modelName().isEmpty(); });
        check(client.modelName() == QStringLiteral("FT-847"),
              "second radio identifies itself before the drop");

        delete rig;                          // the cable falls out
        waitFor([&] { return !client.connected(); });

        check(!client.connected(), "the drop is noticed");
        check(client.modelName().isEmpty(),
              "the model name is FORGOTTEN on disconnect — no stale attribution");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
