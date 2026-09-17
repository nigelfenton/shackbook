#pragma once

// ShackStatus — what ShackBook publishes to MQTT, and how Home Assistant
// discovers it (#24). Pure functions: topic naming, payloads and discovery
// config are all testable without a broker.
//
// Topics, all RETAINED, under <prefix> (default "shackbook/<node>"):
//   availability          online / offline   (offline also via Last Will)
//   radio/connected       ON / OFF
//   radio/transmitting    ON / OFF           (TCI only; drives an ON AIR light)
//   radio/frequency       Hz, e.g. 14074000
//   radio/mode            e.g. FT8, USB, CW
//   radio/band            e.g. 20m
//   qso/count_today       integer, UTC day
//   qso/last              JSON {call,band,mode,freq_mhz,time_utc,country}
//                         — ONLY when "publish QSO details" is on
//
// Home Assistant MQTT discovery: retained config at
//   homeassistant/<component>/shackbook_<node>/<object>/config
// grouping every entity under one "ShackBook <CALL>" device.

#include <QByteArray>
#include <QString>
#include <QVector>

namespace ShackBook {

struct Qso;

namespace ShackStatus {

// A topic- and HA-safe id from a callsign: lower case, [a-z0-9_] only.
// "G0JKN/W3" -> "g0jkn_w3". A '/' left in would split the topic hierarchy.
QString nodeId(const QString& callsign);

QString defaultTopicPrefix(const QString& callsign);

// Subtopics (relative to the prefix).
inline constexpr const char* kConnected    = "radio/connected";
inline constexpr const char* kTransmitting = "radio/transmitting";
inline constexpr const char* kFrequency    = "radio/frequency";
inline constexpr const char* kMode         = "radio/mode";
inline constexpr const char* kBand         = "radio/band";
inline constexpr const char* kCountToday   = "qso/count_today";
inline constexpr const char* kLastQso      = "qso/last";

QByteArray onOff(bool on);
QByteArray frequencyPayload(double mhz);   // Hz as an integer; empty if unknown
// "FT4" for MFSK/FT4 — the name an operator reads, not the ADIF parent mode.
QByteArray modePayload(const QString& mode, const QString& submode);
QByteArray lastQsoPayload(const Qso& q);

struct DiscoveryMessage {
    QString    topic;
    QByteArray payload;   // empty = retained delete (entity removed from HA)
};

// Every discovery config message. With includeQsoDetails false the last-QSO
// entity is DELETED (empty payload) rather than left stale in HA.
QVector<DiscoveryMessage> discoveryMessages(const QString& topicPrefix,
                                            const QString& callsign,
                                            const QString& swVersion,
                                            bool includeQsoDetails);

} // namespace ShackStatus
} // namespace ShackBook
