#include "MqttPacket.h"

namespace ShackBook::Mqtt {

QByteArray encodeRemainingLength(int length)
{
    QByteArray out;
    if (length < 0) length = 0;
    do {
        char byte = static_cast<char>(length % 128);
        length /= 128;
        if (length > 0) byte = static_cast<char>(byte | 0x80);
        out.append(byte);
    } while (length > 0 && out.size() < 4);
    return out;
}

QByteArray encodeString(const QString& s)
{
    QByteArray utf8 = s.toUtf8();
    if (utf8.size() > 0xFFFF) utf8.truncate(0xFFFF);   // protocol limit
    QByteArray out;
    out.append(static_cast<char>((utf8.size() >> 8) & 0xFF));
    out.append(static_cast<char>(utf8.size() & 0xFF));
    out.append(utf8);
    return out;
}

namespace {

QByteArray encodeBinary(const QByteArray& b)
{
    QByteArray out;
    const int n = qMin(b.size(), 0xFFFF);
    out.append(static_cast<char>((n >> 8) & 0xFF));
    out.append(static_cast<char>(n & 0xFF));
    out.append(b.left(n));
    return out;
}

QByteArray withFixedHeader(quint8 typeAndFlags, const QByteArray& body)
{
    QByteArray out;
    out.append(static_cast<char>(typeAndFlags));
    out.append(encodeRemainingLength(body.size()));
    out.append(body);
    return out;
}

} // namespace

QByteArray connectPacket(const ConnectOptions& o)
{
    QByteArray body;
    body.append(encodeString(QStringLiteral("MQTT")));   // protocol name
    body.append(static_cast<char>(4));                    // protocol level 3.1.1

    quint8 flags = 0;
    if (o.cleanSession) flags |= 0x02;
    const bool will = !o.willTopic.isEmpty();
    if (will) {
        flags |= 0x04;                                    // will flag, will QoS 0
        if (o.willRetain) flags |= 0x20;
    }
    // A password without a username is a protocol error in 3.1.1.
    const bool user = !o.username.isEmpty();
    if (user) {
        flags |= 0x80;
        if (!o.password.isEmpty()) flags |= 0x40;
    }
    body.append(static_cast<char>(flags));
    body.append(static_cast<char>((o.keepAliveSec >> 8) & 0xFF));
    body.append(static_cast<char>(o.keepAliveSec & 0xFF));

    body.append(encodeString(o.clientId));
    if (will) {
        body.append(encodeString(o.willTopic));
        body.append(encodeBinary(o.willPayload));
    }
    if (user) {
        body.append(encodeString(o.username));
        if (!o.password.isEmpty()) body.append(encodeBinary(o.password.toUtf8()));
    }
    return withFixedHeader(0x10, body);
}

QByteArray publishPacket(const QString& topic, const QByteArray& payload, bool retain)
{
    QByteArray body = encodeString(topic);
    body.append(payload);                                  // QoS 0: no packet id
    return withFixedHeader(retain ? 0x31 : 0x30, body);
}

QByteArray pingReqPacket()     { return QByteArray::fromHex("c000"); }
QByteArray disconnectPacket()  { return QByteArray::fromHex("e000"); }

std::optional<int> parseConnack(const QByteArray& data, int* consumed)
{
    if (data.size() < 4) return std::nullopt;
    if (static_cast<quint8>(data[0]) != 0x20 || static_cast<quint8>(data[1]) != 0x02)
        return std::nullopt;
    if (consumed) *consumed = 4;
    return static_cast<int>(static_cast<quint8>(data[3]));
}

QString connackText(int rc)
{
    switch (rc) {
    case 0: return QStringLiteral("connected");
    case 1: return QStringLiteral("broker refused: unacceptable protocol version");
    case 2: return QStringLiteral("broker refused: client identifier rejected");
    case 3: return QStringLiteral("broker refused: server unavailable");
    case 4: return QStringLiteral("broker refused: bad username or password");
    case 5: return QStringLiteral("broker refused: not authorised");
    default: return QStringLiteral("broker refused: code %1").arg(rc);
    }
}

} // namespace ShackBook::Mqtt
