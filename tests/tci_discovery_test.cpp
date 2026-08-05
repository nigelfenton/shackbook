// TCI discovery: find real servers, and refuse things that merely listen.
//
// The rule this pins, and the reason the class exists at all: AN OPEN PORT IS
// NOT A RADIO. Plenty of software accepts a TCP connection on 40001 — a replay
// server once squatted that port in this shack and silently stole the radio's
// clients. Discovery therefore requires a real TCI handshake before reporting
// anything, and a port that connects but never speaks TCI must be REJECTED.
//
// Servers here are minimal fakes built on QWebSocketServer, bound to loopback
// on off-band ports so a test run can never reach the air or a real radio.
//
// ⚠ ANTIVIRUS WILL LIKELY FLAG THIS BINARY. A freshly built, unsigned exe that
// opens listening sockets and then probes a range of ports is, behaviourally,
// indistinguishable from a port scanner — Avast/AVG report it as IDP.discovery
// or IDP.Generic (seen on Windows, 2026-08-05). It is a heuristic, not a
// signature match, and the block presents as a plain ctest FAILURE with no
// mention of antivirus anywhere. Run the exe directly to tell the two apart:
// if it passes standalone and fails under ctest, it is the scanner, not the
// code. The fix is an exclusion for the build-tests directory.

#include "TciDiscovery.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTcpServer>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <cstdio>

using namespace ShackLog;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

// A fake TCI server: answers `start;` with a protocol/device announcement,
// exactly as AetherSDR and ExpertSDR do.
class FakeTciServer : public QObject {
public:
    FakeTciServer(const QString& device, quint16 port, QObject* parent = nullptr)
        : QObject(parent), m_device(device)
    {
        m_server = new QWebSocketServer(QStringLiteral("fake-tci"),
                                        QWebSocketServer::NonSecureMode, this);
        m_listening = m_server->listen(QHostAddress::LocalHost, port);
        connect(m_server, &QWebSocketServer::newConnection, this, [this]() {
            QWebSocket* sock = m_server->nextPendingConnection();
            connect(sock, &QWebSocket::textMessageReceived, sock,
                    [this, sock](const QString& msg) {
                        if (!msg.contains(QStringLiteral("start"), Qt::CaseInsensitive))
                            return;
                        sock->sendTextMessage(QStringLiteral("protocol:ExpertSDR3,1.9;"));
                        sock->sendTextMessage(QStringLiteral("device:%1;").arg(m_device));
                        sock->sendTextMessage(QStringLiteral("vfo:0,0,14074000;"));
                    });
        });
    }
    bool listening() const { return m_listening; }

private:
    QWebSocketServer* m_server{nullptr};
    QString m_device;
    bool m_listening{false};
};

// A decoy: accepts TCP and says nothing at all. Must never be reported.
class SilentServer : public QObject {
public:
    SilentServer(quint16 port, QObject* parent = nullptr) : QObject(parent)
    {
        m_server = new QTcpServer(this);
        m_listening = m_server->listen(QHostAddress::LocalHost, port);
    }
    bool listening() const { return m_listening; }

private:
    QTcpServer* m_server{nullptr};
    bool m_listening{false};
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Off-band, loopback-only, well away from 40001/50001 so a test run can
    // never touch a real radio's port even by accident.
    constexpr quint16 kRealA  = 45801;
    constexpr quint16 kRealB  = 45802;
    constexpr quint16 kDecoy  = 45803;
    constexpr quint16 kClosed = 45804;   // nothing listens here

    FakeTciServer a(QStringLiteral("AetherSDR"), kRealA);
    FakeTciServer b(QStringLiteral("ExpertSDR3"), kRealB);
    SilentServer  d(kDecoy);

    check(a.listening() && b.listening() && d.listening(),
          "fake servers bound to their loopback ports");
    if (!a.listening() || !b.listening() || !d.listening()) {
        std::printf("\nFAILED (could not bind test ports)\n");
        return 1;
    }

    TciDiscovery discovery;
    QList<TciServerInfo> found;
    QEventLoop loop;
    QObject::connect(&discovery, &TciDiscovery::finished, &loop,
                     [&](const QList<TciServerInfo>& servers) {
                         found = servers;
                         loop.quit();
                     });

    // Safety net: never hang a CI run if discovery fails to emit finished().
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(15000);

    discovery.scan({QStringLiteral("127.0.0.1")},
                   {kRealA, kRealB, kDecoy, kClosed}, /*timeoutMs*/ 1500);
    loop.exec();

    check(found.size() == 2, "exactly two servers reported");

    bool sawA = false, sawB = false, sawDecoy = false, sawClosed = false;
    for (const TciServerInfo& s : found) {
        if (s.port == kRealA)  { sawA = true; check(s.device == QStringLiteral("AetherSDR"),
                                                    "first server reports its device name"); }
        if (s.port == kRealB)  { sawB = true; check(s.device == QStringLiteral("ExpertSDR3"),
                                                    "second server reports its device name"); }
        if (s.port == kDecoy)  sawDecoy  = true;
        if (s.port == kClosed) sawClosed = true;
    }

    check(sawA, "the TCI server on the first port was found");
    check(sawB, "the TCI server on the second port was found");
    check(!sawDecoy,
          "a port that ACCEPTS TCP but never speaks TCI is REJECTED");
    check(!sawClosed, "a closed port is not reported");

    for (const TciServerInfo& s : found)
        check(s.label().contains(QStringLiteral("127.0.0.1")),
              "label carries host and port for the operator to recognise");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
