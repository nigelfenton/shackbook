#pragma once

// MqttPacket — the few MQTT 3.1.1 packets ShackBook needs, as pure functions.
//
// ShackBook only PUBLISHES shack status to a LAN broker (#24): no subscribe,
// no QoS 1/2, no TLS. That is a handful of packets, so rather than pull in the
// Qt MQTT module (check its licence before ever reaching for it — it is not
// offered under the LGPL, and ShackBook is MIT) the wire format lives here,
// where every byte can be tested without a broker.
//
// Reference: MQTT Version 3.1.1, OASIS Standard, sections 2.2 (fixed header,
// remaining length), 3.1 (CONNECT), 3.2 (CONNACK), 3.3 (PUBLISH),
// 3.12 (PINGREQ), 3.14 (DISCONNECT).

#include <QByteArray>
#include <QString>

#include <optional>

namespace ShackBook::Mqtt {

// Remaining-length varint (§2.2.3): 1-4 bytes, max 268,435,455.
QByteArray encodeRemainingLength(int length);

// UTF-8 string with a 2-byte big-endian length prefix (§1.5.3).
QByteArray encodeString(const QString& s);

struct ConnectOptions {
    QString    clientId;
    QString    username;          // empty: no username (and no password)
    QString    password;
    quint16    keepAliveSec{60};
    bool       cleanSession{true};
    // Last Will: published by the BROKER if we vanish without DISCONNECT.
    QString    willTopic;         // empty: no will
    QByteArray willPayload;
    bool       willRetain{true};
};

QByteArray connectPacket(const ConnectOptions& o);

// QoS 0 PUBLISH. `retain` asks the broker to keep the last value for new
// subscribers — what makes a dashboard show state immediately on reload.
QByteArray publishPacket(const QString& topic, const QByteArray& payload, bool retain);

QByteArray pingReqPacket();
QByteArray disconnectPacket();

// CONNACK return code (§3.2.2.3). 0 = accepted.
enum class ConnackResult : int {
    Accepted              = 0,
    BadProtocolVersion    = 1,
    IdentifierRejected    = 2,
    ServerUnavailable     = 3,
    BadUsernameOrPassword = 4,
    NotAuthorized         = 5,
};

// Parse a CONNACK at the start of `data`. Empty when `data` does not yet hold
// a complete, well-formed CONNACK. On success `consumed` is set to its size.
std::optional<int> parseConnack(const QByteArray& data, int* consumed = nullptr);

QString connackText(int returnCode);

} // namespace ShackBook::Mqtt
