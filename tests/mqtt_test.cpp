// mqtt_test — shack status over MQTT for Home Assistant (#24).
//
// Three layers, none needing a real broker:
//   1. MqttPacket: exact MQTT 3.1.1 bytes (remaining length, CONNECT flags and
//      field order, PUBLISH retain bit, CONNACK parsing).
//   2. ShackStatus: topic-safe ids, payloads, and Home Assistant discovery
//      config (valid JSON, shared device, last-QSO entity removed when QSO
//      details are off).
//   3. MqttPublisher against a fake broker on loopback: CONNECT carries the
//      credentials and the offline Last Will; state cached before connecting
//      is sent after CONNACK; identical values are not re-sent; a refused
//      login publishes nothing; a clean shutdown publishes offline.

#include "MqttPacket.h"
#include "MqttPublisher.h"
#include "Qso.h"
#include "ShackStatus.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdio>
#include <functional>
#include <memory>

using namespace ShackBook;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

bool waitFor(const std::function<bool()>& done, int ms = 4000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

void pause(int ms) { waitFor([] { return false; }, ms); }

// ── 1. Packets ──────────────────────────────────────────────────────────────

void packets()
{
    std::printf("\n-- MqttPacket --\n");
    using namespace Mqtt;

    check(encodeRemainingLength(0)       == QByteArray::fromHex("00"),       "remaining length 0");
    check(encodeRemainingLength(127)     == QByteArray::fromHex("7f"),       "remaining length 127 (1 byte max)");
    check(encodeRemainingLength(128)     == QByteArray::fromHex("8001"),     "remaining length 128 (2 bytes)");
    check(encodeRemainingLength(16383)   == QByteArray::fromHex("ff7f"),     "remaining length 16383 (2 bytes max)");
    check(encodeRemainingLength(16384)   == QByteArray::fromHex("808001"),   "remaining length 16384 (3 bytes)");
    check(encodeRemainingLength(2097152) == QByteArray::fromHex("80808001"), "remaining length 2097152 (4 bytes)");

    check(encodeString(QStringLiteral("MQTT")) == QByteArray::fromHex("00044d515454"),
          "string: 2-byte big-endian length prefix");

    {
        ConnectOptions o;
        o.clientId = QStringLiteral("c");
        o.keepAliveSec = 60;
        // No will, no credentials: flags = clean session only.
        const QByteArray expect = QByteArray::fromHex(
            "10"              // CONNECT
            "0d"              // remaining length 13
            "00044d515454"    // "MQTT"
            "04"              // level 3.1.1
            "02"              // clean session
            "003c"            // keepalive 60
            "000163");        // client id "c"
        check(connectPacket(o) == expect, "CONNECT minimal: exact bytes");
    }
    {
        ConnectOptions o;
        o.clientId = QStringLiteral("c");
        o.username = QStringLiteral("u");
        o.password = QStringLiteral("p");
        o.willTopic = QStringLiteral("t");
        o.willPayload = QByteArrayLiteral("off");
        o.willRetain = true;
        const QByteArray expect = QByteArray::fromHex(
            "10" "1b"
            "00044d515454" "04"
            "e6"              // user 0x80 | pass 0x40 | will retain 0x20 | will 0x04 | clean 0x02
            "003c"
            "000163"          // client id
            "000174"          // will topic "t"
            "00036f6666"      // will payload "off"
            "000175"          // username "u"
            "000170");        // password "p"
        check(connectPacket(o) == expect, "CONNECT with will + credentials: flags and field ORDER");
    }
    {
        ConnectOptions o;
        o.clientId = QStringLiteral("c");
        o.password = QStringLiteral("p");   // no username
        const QByteArray pkt = connectPacket(o);
        check((static_cast<quint8>(pkt[9]) & 0xC0) == 0,
              "a password without a username is not sent (protocol error in 3.1.1)");
    }

    check(publishPacket(QStringLiteral("a/b"), QByteArrayLiteral("ON"), true)
              == QByteArray::fromHex("3107" "0003612f62" "4f4e"),
          "PUBLISH retained QoS 0: 0x31, topic, payload, no packet id");
    check(static_cast<quint8>(publishPacket(QStringLiteral("a"), "x", false)[0]) == 0x30,
          "PUBLISH not retained: 0x30");
    check(pingReqPacket() == QByteArray::fromHex("c000"), "PINGREQ");
    check(disconnectPacket() == QByteArray::fromHex("e000"), "DISCONNECT");

    int used = 0;
    check(parseConnack(QByteArray::fromHex("20020000"), &used) == 0 && used == 4, "CONNACK accepted");
    check(parseConnack(QByteArray::fromHex("20020004")) == 4, "CONNACK bad username/password");
    check(!parseConnack(QByteArray::fromHex("2002")), "partial CONNACK: not yet");
    check(!parseConnack(QByteArray::fromHex("30020000")), "not a CONNACK");
}

// ── 2. ShackStatus ──────────────────────────────────────────────────────────

void status()
{
    std::printf("\n-- ShackStatus --\n");
    using namespace ShackStatus;

    check(nodeId(QStringLiteral("G0JKN")) == QStringLiteral("g0jkn"), "node id: lower case");
    check(nodeId(QStringLiteral("G0JKN/W3")) == QStringLiteral("g0jkn_w3"),
          "node id: '/' cannot split the topic hierarchy");
    check(nodeId(QStringLiteral(" /W3/P/ ")) == QStringLiteral("w3_p"), "node id: trimmed, no leading/trailing '_'");
    check(nodeId(QString()) == QStringLiteral("station"), "node id: empty call has a fallback");
    check(defaultTopicPrefix(QStringLiteral("G0JKN")) == QStringLiteral("shackbook/g0jkn"), "default prefix");

    check(frequencyPayload(14.074) == QByteArrayLiteral("14074000"), "frequency published in Hz");
    check(frequencyPayload(0.0).isEmpty(), "unknown frequency: nothing");
    check(modePayload(QStringLiteral("MFSK"), QStringLiteral("FT4")) == QByteArrayLiteral("FT4"),
          "mode: submode wins (FT4, not MFSK)");
    check(modePayload(QStringLiteral("usb"), QString()) == QByteArrayLiteral("USB"), "mode: upper case");

    Qso q;
    q.call = QStringLiteral("K1ABC"); q.band = QStringLiteral("20m"); q.freq = 14.074;
    q.mode = QStringLiteral("FT8"); q.qsoDate = QStringLiteral("20260916"); q.timeOn = QStringLiteral("2104");
    const QJsonObject last = QJsonDocument::fromJson(lastQsoPayload(q)).object();
    check(last.value("call").toString() == QStringLiteral("K1ABC")
              && last.value("time_utc").toString() == QStringLiteral("2026-09-16T21:04:00Z"),
          "last QSO JSON: call and ISO-8601 UTC time from HHMM");

    const auto withQso = discoveryMessages(QStringLiteral("shackbook/g0jkn"), QStringLiteral("G0JKN"),
                                           QStringLiteral("0.8.0"), true);
    bool allValid = !withQso.isEmpty(), oneDevice = true, availability = true;
    QString deviceId;
    bool sawTx = false, sawFreqClass = false;
    bool freqUnitMhz = false, freqTemplateConverts = false, freqNoUnsupportedKey = false;
    for (const auto& m : withQso) {
        const QJsonDocument d = QJsonDocument::fromJson(m.payload);
        if (!d.isObject()) { allValid = false; continue; }
        const QJsonObject c = d.object();
        const QString id = c.value("device").toObject().value("identifiers").toArray().at(0).toString();
        if (deviceId.isEmpty()) deviceId = id; else if (id != deviceId) oneDevice = false;
        if (c.value("availability_topic").toString() != QStringLiteral("shackbook/g0jkn/availability"))
            availability = false;
        if (m.topic == QStringLiteral("homeassistant/binary_sensor/shackbook_g0jkn/transmitting/config")
            && c.value("state_topic").toString() == QStringLiteral("shackbook/g0jkn/radio/transmitting"))
            sawTx = true;
        if (c.value("unique_id").toString() == QStringLiteral("shackbook_g0jkn_frequency")
            && c.value("device_class").toString() == QStringLiteral("frequency"))
            sawFreqClass = true;
        if (c.value("unique_id").toString() == QStringLiteral("shackbook_g0jkn_frequency")) {
            // First live run (2026-09-17) showed "10,000,000.000 Hz": HA's MQTT
            // sensor ignores suggested_unit_of_measurement. Display MHz via the
            // unit + a template that converts the Hz payload.
            freqUnitMhz = c.value("unit_of_measurement").toString() == QStringLiteral("MHz");
            freqTemplateConverts = c.value("value_template").toString().contains(QStringLiteral("1000000"));
            freqNoUnsupportedKey = !c.contains(QStringLiteral("suggested_unit_of_measurement"));
        }
    }
    check(allValid, "discovery: every config is a JSON object");
    check(oneDevice && deviceId == QStringLiteral("shackbook_g0jkn"), "discovery: all entities share one device");
    check(availability, "discovery: every entity uses the availability topic");
    check(sawTx, "discovery: transmitting binary_sensor at the HA topic, reading radio/transmitting");
    check(sawFreqClass, "discovery: frequency has device_class frequency");
    check(freqUnitMhz, "discovery: frequency is shown in MHz");
    check(freqTemplateConverts, "discovery: frequency template converts the Hz payload to MHz");
    check(freqNoUnsupportedKey, "discovery: no suggested_unit_of_measurement (HA's MQTT sensor ignores it)");

    const auto noQso = discoveryMessages(QStringLiteral("shackbook/g0jkn"), QStringLiteral("G0JKN"), {}, false);
    bool lastQsoDeleted = false;
    for (const auto& m : noQso)
        if (m.topic.endsWith(QStringLiteral("/last_qso/config")) && m.payload.isEmpty()) lastQsoDeleted = true;
    check(lastQsoDeleted, "discovery: QSO details off -> last-QSO entity is DELETED, not left stale");
}

// ── 3. Publisher vs a fake broker ───────────────────────────────────────────

struct Packet { quint8 header; QByteArray body; };

// Split a byte stream into MQTT packets (fixed header + remaining length).
QVector<Packet> splitPackets(QByteArray& buf)
{
    QVector<Packet> out;
    for (;;) {
        if (buf.size() < 2) break;
        int mult = 1, len = 0, i = 1;
        bool complete = false;
        while (i < buf.size() && i <= 4) {
            const quint8 b = static_cast<quint8>(buf[i]);
            len += (b & 0x7f) * mult;
            mult *= 128;
            ++i;
            if (!(b & 0x80)) { complete = true; break; }
        }
        if (!complete || buf.size() < i + len) break;
        out.append({static_cast<quint8>(buf[0]), buf.mid(i, len)});
        buf.remove(0, i + len);
    }
    return out;
}

QString readStr(const QByteArray& b, int& pos)
{
    const int n = (static_cast<quint8>(b[pos]) << 8) | static_cast<quint8>(b[pos + 1]);
    const QString s = QString::fromUtf8(b.mid(pos + 2, n));
    pos += 2 + n;
    return s;
}

class FakeBroker : public QObject {
public:
    explicit FakeBroker(quint16 port, int connackCode) : m_code(connackCode)
    {
        m_ok = m_server.listen(QHostAddress::LocalHost, port);
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            m_sock = m_server.nextPendingConnection();
            connect(m_sock, &QTcpSocket::readyRead, this, [this]() {
                m_buf.append(m_sock->readAll());
                for (const Packet& p : splitPackets(m_buf)) onPacket(p);
            });
        });
    }
    bool ok() const { return m_ok; }

    // What the broker saw.
    int     connects{0};
    QString clientId, username, password, willTopic;
    QByteArray willPayload;
    bool    willRetain{false};
    QVector<QPair<QString, QByteArray>> publishes;   // (topic, payload), all retained
    bool    allRetained{true};
    bool    sawDisconnect{false};

    int count(const QString& topic) const
    {
        int n = 0;
        for (const auto& p : publishes) if (p.first == topic) ++n;
        return n;
    }
    QByteArray last(const QString& topic) const
    {
        for (int i = publishes.size() - 1; i >= 0; --i)
            if (publishes[i].first == topic) return publishes[i].second;
        return {};
    }

private:
    void onPacket(const Packet& p)
    {
        const quint8 type = p.header >> 4;
        if (type == 1) {                                     // CONNECT
            ++connects;
            int pos = 0;
            readStr(p.body, pos);                            // "MQTT"
            ++pos;                                           // level
            const quint8 flags = static_cast<quint8>(p.body[pos]);
            pos += 3;                                        // flags + keepalive
            clientId = readStr(p.body, pos);
            if (flags & 0x04) {
                willTopic = readStr(p.body, pos);
                willPayload = readStr(p.body, pos).toUtf8();
                willRetain = flags & 0x20;
            }
            if (flags & 0x80) username = readStr(p.body, pos);
            if (flags & 0x40) password = readStr(p.body, pos);
            m_sock->write(QByteArray::fromHex("2002") + QByteArray(1, 0)
                          + QByteArray(1, static_cast<char>(m_code)));
        } else if (type == 3) {                              // PUBLISH
            int pos = 0;
            const QString topic = readStr(p.body, pos);
            publishes.append({topic, p.body.mid(pos)});
            if (!(p.header & 0x01)) allRetained = false;
        } else if (type == 14) {                             // DISCONNECT
            sawDisconnect = true;
        } else if (type == 12) {                             // PINGREQ
            m_sock->write(QByteArray::fromHex("d000"));
        }
    }

    QTcpServer  m_server;
    QTcpSocket* m_sock{nullptr};
    QByteArray  m_buf;
    int         m_code;
    bool        m_ok{false};
};

MqttConfig configFor(quint16 port)
{
    MqttConfig c;
    c.enabled = true;
    c.host = QStringLiteral("127.0.0.1");
    c.port = port;
    c.username = QStringLiteral("shackbook");
    c.password = QStringLiteral("s3cret");
    c.clientId = QStringLiteral("shackbook-g0jkn-test");
    c.topicPrefix = QStringLiteral("shackbook/g0jkn");
    return c;
}

void publisher()
{
    std::printf("\n-- MqttPublisher vs a fake broker --\n");

    {
        constexpr quint16 kPort = 45861;
        FakeBroker broker(kPort, /*CONNACK*/ 0);
        check(broker.ok(), "fake broker bound to loopback");
        if (!broker.ok()) return;

        auto pub = std::make_unique<MqttPublisher>();
        // Published BEFORE connecting: must be cached and sent after CONNACK.
        pub->publishState(QStringLiteral("radio/frequency"), QByteArrayLiteral("7074000"));
        pub->configure(configFor(kPort));
        pub->publishState(QStringLiteral("radio/frequency"), QByteArrayLiteral("14074000"));

        check(waitFor([&] { return pub->isConnected(); }), "publisher connects after CONNACK 0");
        check(broker.clientId == QStringLiteral("shackbook-g0jkn-test"), "CONNECT: client id");
        check(broker.username == QStringLiteral("shackbook") && broker.password == QStringLiteral("s3cret"),
              "CONNECT: username and password");
        check(broker.willTopic == QStringLiteral("shackbook/g0jkn/availability")
                  && broker.willPayload == QByteArrayLiteral("offline") && broker.willRetain,
              "CONNECT: retained Last Will availability=offline");

        check(waitFor([&] { return broker.count("shackbook/g0jkn/radio/frequency") >= 1; }),
              "state cached before connecting is sent after CONNACK");
        check(broker.last("shackbook/g0jkn/availability") == QByteArrayLiteral("online"),
              "availability=online after connecting");
        check(broker.last("shackbook/g0jkn/radio/frequency") == QByteArrayLiteral("14074000")
                  && broker.count("shackbook/g0jkn/radio/frequency") == 1,
              "only the LATEST cached value is sent, once");

        pub->publishState(QStringLiteral("radio/transmitting"), QByteArrayLiteral("ON"));
        pub->publishState(QStringLiteral("radio/transmitting"), QByteArrayLiteral("ON"));
        check(waitFor([&] { return broker.count("shackbook/g0jkn/radio/transmitting") >= 1; }),
              "live publish reaches the broker");
        pause(200);
        check(broker.count("shackbook/g0jkn/radio/transmitting") == 1, "an identical repeat is not re-sent");

        pub->publishAbsolute(QStringLiteral("homeassistant/sensor/x/y/config"), QByteArray{});
        check(waitFor([&] { return broker.count("homeassistant/sensor/x/y/config") == 1; })
                  && broker.last("homeassistant/sensor/x/y/config").isEmpty(),
              "empty payload is sent as a retained delete");
        check(broker.allRetained, "every publish is retained");

        MqttConfig off = configFor(kPort);
        off.enabled = false;
        pub->configure(off);
        check(waitFor([&] { return broker.sawDisconnect; }), "disabling sends DISCONNECT");
        check(broker.last("shackbook/g0jkn/availability") == QByteArrayLiteral("offline"),
              "a clean disconnect publishes availability=offline explicitly (it suppresses the will)");
        check(!pub->isConnected(), "publisher reports disconnected");
    }

    {
        constexpr quint16 kPort = 45862;
        FakeBroker broker(kPort, /*CONNACK*/ 4);   // bad username or password
        if (!broker.ok()) { check(false, "second fake broker bound"); return; }
        MqttPublisher pub;
        pub.setBackoffScaleForTest(100.0);         // no quick retry during this check
        pub.publishState(QStringLiteral("radio/band"), QByteArrayLiteral("20m"));
        pub.configure(configFor(kPort));
        check(waitFor([&] { return broker.connects >= 1 && !pub.lastError().isEmpty(); }),
              "refused login is reported");
        check(pub.lastError().contains(QStringLiteral("username or password")), "error names the cause");
        pause(200);
        check(!pub.isConnected() && broker.publishes.isEmpty(), "a refused login publishes nothing");
    }

    {
        MqttPublisher pub;
        MqttConfig c = configFor(45863);
        c.host.clear();
        pub.configure(c);
        check(!pub.isConnected() && !pub.lastError().isEmpty(),
              "enabled with no broker host: clear error, no connection attempt");
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    packets();
    status();
    publisher();

    if (failures == 0) {
        std::printf("\nmqtt_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "\nmqtt_test: %d failure(s)\n", failures);
    return 1;
}
