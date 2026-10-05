// N1MM+ / DXLog spots over UDP (#11): the parser, SpotIndex's per-band delete,
// and the client end to end on loopback.
//
// ⭐ The cases that matter most:
//   • a datagram cut short must be refused, not read as a spot at 0.140 MHz;
//   • a delete for one band must never remove the same call on another;
//   • a port another program holds must be reported, not sit quietly.
//
// ⚠ Antivirus may flag this binary: it opens listening sockets. See
// tests/tci_discovery_test.cpp for the full note.

#include "N1mmSpotClient.h"
#include "N1mmSpotParser.h"
#include "SpotIndex.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUdpSocket>

#include <cstdio>

using namespace ShackBook;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

template <typename Pred>
void waitFor(Pred p, int maxMs = 3000)
{
    QEventLoop loop;
    QTimer tick;
    tick.setInterval(10);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&]() { if (p()) loop.quit(); });
    QTimer::singleShot(maxMs, &loop, &QEventLoop::quit);
    tick.start();
    loop.exec();
}

const QByteArray kAdd =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<spot>\n"
    "  <app>N1MM</app>\n"
    "  <StationName>RUN1</StationName>\n"
    "  <dxcall>k1abc</dxcall>\n"
    "  <frequency>14025.1</frequency>\n"
    "  <spottercall>w1xyz</spottercall>\n"
    "  <comment>CQ TEST</comment>\n"
    "  <action>add</action>\n"
    "  <mode>CW</mode>\n"
    "  <statuslist>single mult</statuslist>\n"
    "  <timestamp>2026-10-05 14:02:11</timestamp>\n"
    "</spot>\n";

void parserCases()
{
    std::printf("\n-- parser --\n");
    N1mmSpot s;

    check(parseN1mmSpot(kAdd, s) == N1mmParse::Spot, "a full add packet is a spot");
    check(s.spot.call == QStringLiteral("K1ABC"), "the call is upper-cased");
    check(qAbs(s.spot.freqMhz - 14.0251) < 1e-9, "the frequency is converted from kHz to MHz");
    check(s.spot.mode == QStringLiteral("CW"), "mode is read");
    check(s.spot.comment == QStringLiteral("CQ TEST"), "comment is read");
    check(s.spot.spotter == QStringLiteral("W1XYZ"), "spotter is read and upper-cased");
    check(s.spot.status == QStringLiteral("mult"), "\"single mult\" becomes the mult flag");
    check(s.spot.source == QStringLiteral("N1MM: RUN1"), "the source names the logger station");
    check(!s.remove, "an add is not a delete");

    QByteArray del = kAdd;
    del.replace("<action>add</action>", "<action>delete</action>");
    check(parseN1mmSpot(del, s) == N1mmParse::Spot && s.remove, "<action>delete</action> is a delete");

    QByteArray noAction = kAdd;
    noAction.replace("  <action>add</action>\n", "");
    check(parseN1mmSpot(noAction, s) == N1mmParse::Spot && !s.remove, "no <action> means add");

    const QByteArray mixedCase =
        "<SPOT><DXCALL>G0JKN</DXCALL><Frequency>7025</Frequency><stationname>S&amp;P</stationname></SPOT>";
    check(parseN1mmSpot(mixedCase, s) == N1mmParse::Spot && s.spot.call == QStringLiteral("G0JKN")
              && qAbs(s.spot.freqMhz - 7.025) < 1e-9,
          "element names match in any case");
    check(s.spot.source == QStringLiteral("N1MM: S&P"), "XML entities are decoded");
    check(s.spot.status.isEmpty(), "no status list means no flag");

    const QByteArray statusOnly =
        "<spot><dxcall>K2AA</dxcall><frequency>21025</frequency><status>new qso</status></spot>";
    check(parseN1mmSpot(statusOnly, s) == N1mmParse::Spot && s.spot.status == QStringLiteral("new"),
          "<status> is used when there is no <statuslist>");

    // ⛔ Truncated: UDP loss cut the datagram mid-frequency.
    check(parseN1mmSpot("<spot><dxcall>K1ABC</dxcall><frequency>140", s) == N1mmParse::Malformed,
          "a datagram cut short is refused, not read as 0.140 MHz");
    QByteArray cutAfterFreq = kAdd.left(kAdd.indexOf("<spottercall>"));
    check(parseN1mmSpot(cutAfterFreq, s) == N1mmParse::Malformed,
          "a datagram cut after a complete frequency is refused too");

    check(parseN1mmSpot("<RadioInfo><Freq>1402500</Freq></RadioInfo>", s) == N1mmParse::NotSpot,
          "another N1MM document is 'not a spot', not an error");
    check(parseN1mmSpot("\x01\x02 random bytes", s) == N1mmParse::Malformed, "garbage is malformed");
    check(parseN1mmSpot("<spot><frequency>14025</frequency></spot>", s) == N1mmParse::Malformed,
          "a spot without a call is refused");
    check(parseN1mmSpot("<spot><dxcall>K1ABC</dxcall><frequency>0</frequency></spot>", s)
              == N1mmParse::Malformed,
          "a zero frequency is refused");
    check(parseN1mmSpot("<spot><dxcall>K1ABC</dxcall><frequency>abc</frequency></spot>", s)
              == N1mmParse::Malformed,
          "a non-numeric frequency is refused");
    check(parseN1mmSpot("<spot><dxcall>NOT A CALL</dxcall><frequency>14025</frequency></spot>", s)
              == N1mmParse::Malformed,
          "a call with spaces is refused");

    std::printf("\n-- status priority --\n");
    check(n1mmStatusFlag(QStringLiteral("new qso dupe")) == QStringLiteral("dupe"), "dupe beats new");
    check(n1mmStatusFlag(QStringLiteral("double mult bust")) == QStringLiteral("bust"), "bust beats mult");
    check(n1mmStatusFlag(QStringLiteral("double mult")) == QStringLiteral("mult"), "double mult is mult");
    check(n1mmStatusFlag(QStringLiteral("CQ")) == QStringLiteral("cq"), "flags are case-insensitive");
    check(n1mmStatusFlag(QString()).isEmpty(), "nothing is no flag");
    check(n1mmStatusFlag(QStringLiteral("multiplier")).isEmpty(), "only whole tokens count");
}

SpotData spotAt(const char* call, double mhz, const char* mode = "CW")
{
    SpotData s;
    s.call = QString::fromLatin1(call);
    s.freqMhz = mhz;
    s.mode = QString::fromLatin1(mode);
    return s;
}

void spotIndexRemove()
{
    std::printf("\n-- SpotIndex::remove --\n");
    SpotIndex idx;
    int removedSignals = 0;
    QObject::connect(&idx, &SpotIndex::spotsRemoved, [&](int n) { removedSignals += n; });

    idx.addOrUpdate(spotAt("K1ABC", 14.025));
    check(idx.remove(QStringLiteral("K1ABC"), 14.030), "a delete on the same band removes the spot");
    check(idx.size() == 0, "and the index is empty");
    check(!idx.findAt(14.025, QStringLiteral("CW")).has_value(), "and the bucket no longer finds it");
    check(removedSignals == 1, "spotsRemoved fires once");

    idx.addOrUpdate(spotAt("K1ABC", 7.025));
    check(!idx.remove(QStringLiteral("K1ABC"), 14.025),
          "a delete for 20m does NOT remove the same call on 40m");
    check(idx.size() == 1 && idx.findAt(7.025, QStringLiteral("CW")).has_value(),
          "the 40m spot is still held and findable");

    check(!idx.remove(QStringLiteral("W9ZZZ"), 7.025), "deleting a call that isn't held does nothing");
    check(removedSignals == 1, "and emits nothing");

    idx.addOrUpdate(spotAt("K3OUT", 15.5));   // outside every amateur band
    check(!idx.remove(QStringLiteral("K3OUT"), 16.0), "out of band: more than 100 kHz apart is kept");
    check(idx.remove(QStringLiteral("K3OUT"), 15.55), "out of band: within 100 kHz is removed");
}

quint16 freeUdpPort()
{
    QUdpSocket probe;
    probe.bind(QHostAddress::LocalHost, 0);
    const quint16 p = probe.localPort();
    probe.close();
    return p;
}

void clientEndToEnd()
{
    std::printf("\n-- client on loopback --\n");
    const quint16 port = freeUdpPort();
    N1mmSpotClient client;

    QList<SpotData> received;
    QList<QPair<QString, double>> deleted;
    QObject::connect(&client, &N1mmSpotClient::spotReceived,
                     [&](const SpotData& s) { received << s; });
    QObject::connect(&client, &N1mmSpotClient::spotDeleted,
                     [&](const QString& c, double f) { deleted << qMakePair(c, f); });

    check(client.start(port), "the client listens on a free port");
    check(client.listening(), "and reports that it is listening");

    QUdpSocket sender;
    sender.writeDatagram(kAdd, QHostAddress::LocalHost, port);
    waitFor([&] { return !received.isEmpty(); });
    check(received.size() == 1 && received.first().call == QStringLiteral("K1ABC"),
          "an add datagram arrives as a spot");
    if (!received.isEmpty())
        check(received.first().receivedAt.isValid(), "and is stamped with when it arrived");

    QByteArray del = kAdd;
    del.replace("<action>add</action>", "<action>delete</action>");
    sender.writeDatagram(del, QHostAddress::LocalHost, port);
    waitFor([&] { return !deleted.isEmpty(); });
    check(deleted.size() == 1 && deleted.first().first == QStringLiteral("K1ABC")
              && qAbs(deleted.first().second - 14.0251) < 1e-9,
          "a delete datagram arrives as a delete, with its frequency");

    sender.writeDatagram("<RadioInfo><Freq>1402500</Freq></RadioInfo>", QHostAddress::LocalHost, port);
    sender.writeDatagram("\x01\x02 junk", QHostAddress::LocalHost, port);
    waitFor([&] { return client.malformed() >= 1; });
    check(client.malformed() == 1, "junk is counted as malformed; another N1MM document is not");
    check(received.size() == 1 && deleted.size() == 1, "neither becomes a spot");

    client.stop();
    check(!client.listening(), "stop() releases the port");
}

void portAlreadyTaken()
{
    std::printf("\n-- port already taken --\n");
    QUdpSocket holder;   // stands in for SmartSDR CAT holding the port
    check(holder.bind(QHostAddress::AnyIPv4, 0, QAbstractSocket::DontShareAddress),
          "another program holds a port");
    const quint16 port = holder.localPort();

    N1mmSpotClient client;
    QString reported;
    QObject::connect(&client, &N1mmSpotClient::listeningChanged,
                     [&](bool, const QString& e) { reported = e; });
    check(!client.start(port), "the client refuses to share it");
    check(!client.listening(), "and is not listening");
    check(client.lastError().contains(QString::number(port)), "the error names the port");
    check(!reported.isEmpty(), "and is signalled, so the operator sees it");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    parserCases();
    spotIndexRemove();
    clientEndToEnd();
    portAlreadyTaken();

    if (failures == 0) {
        std::printf("\nn1mm_spot_test: all checks passed\n");
        return 0;
    }
    std::printf("\nn1mm_spot_test: %d check(s) FAILED\n", failures);
    return 1;
}
