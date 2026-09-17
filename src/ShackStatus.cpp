#include "ShackStatus.h"

#include "Qso.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ShackBook::ShackStatus {

QString nodeId(const QString& callsign)
{
    QString out;
    bool lastUnderscore = false;
    for (const QChar c : callsign.trimmed().toLower()) {
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
                        || (c >= QLatin1Char('0') && c <= QLatin1Char('9'));
        if (ok) {
            out.append(c);
            lastUnderscore = false;
        } else if (!lastUnderscore && !out.isEmpty()) {
            out.append(QLatin1Char('_'));
            lastUnderscore = true;
        }
    }
    while (out.endsWith(QLatin1Char('_'))) out.chop(1);
    return out.isEmpty() ? QStringLiteral("station") : out;
}

QString defaultTopicPrefix(const QString& callsign)
{
    return QStringLiteral("shackbook/") + nodeId(callsign);
}

QByteArray onOff(bool on)
{
    return on ? QByteArrayLiteral("ON") : QByteArrayLiteral("OFF");
}

QByteArray frequencyPayload(double mhz)
{
    if (!(mhz > 0.0)) return {};
    return QByteArray::number(static_cast<qint64>(mhz * 1.0e6 + 0.5));
}

QByteArray modePayload(const QString& mode, const QString& submode)
{
    const QString s = submode.trimmed().isEmpty() ? mode.trimmed() : submode.trimmed();
    return s.toUpper().toUtf8();
}

QByteArray lastQsoPayload(const Qso& q)
{
    QJsonObject o;
    o.insert(QStringLiteral("call"), q.call);
    o.insert(QStringLiteral("band"), q.band);
    o.insert(QStringLiteral("mode"), QString::fromUtf8(modePayload(q.mode, q.submode)));
    if (q.freq > 0.0) o.insert(QStringLiteral("freq_mhz"), q.freq);
    // ISO-8601 UTC from ADIF date + time (HHMM or HHMMSS).
    if (q.qsoDate.size() == 8 && (q.timeOn.size() == 4 || q.timeOn.size() == 6)) {
        const QString t = q.timeOn.size() == 4 ? q.timeOn + QStringLiteral("00") : q.timeOn;
        o.insert(QStringLiteral("time_utc"),
                 QStringLiteral("%1-%2-%3T%4:%5:%6Z")
                     .arg(q.qsoDate.left(4), q.qsoDate.mid(4, 2), q.qsoDate.mid(6, 2),
                          t.left(2), t.mid(2, 2), t.mid(4, 2)));
    }
    if (!q.country.isEmpty()) o.insert(QStringLiteral("country"), q.country);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

namespace {

struct Entity {
    const char* component;   // sensor / binary_sensor
    const char* object;      // object id, unique within the device
    const char* name;
    const char* subtopic;
    const char* icon;
};

} // namespace

QVector<DiscoveryMessage> discoveryMessages(const QString& topicPrefix,
                                            const QString& callsign,
                                            const QString& swVersion,
                                            bool includeQsoDetails)
{
    const QString node = nodeId(callsign);
    const QString uidBase = QStringLiteral("shackbook_") + node;

    QJsonObject device;
    device.insert(QStringLiteral("identifiers"), QJsonArray{uidBase});
    device.insert(QStringLiteral("name"),
                  QStringLiteral("ShackBook %1").arg(callsign.trimmed().toUpper()));
    device.insert(QStringLiteral("manufacturer"), QStringLiteral("ShackBook"));
    device.insert(QStringLiteral("model"), QStringLiteral("Logbook"));
    if (!swVersion.isEmpty()) device.insert(QStringLiteral("sw_version"), swVersion);

    const QString availability = topicPrefix + QStringLiteral("/availability");

    static const Entity kEntities[] = {
        {"binary_sensor", "transmitting", "Transmitting",     kTransmitting, "mdi:radio-tower"},
        {"binary_sensor", "connected",    "Radio connected",  kConnected,    nullptr},
        {"sensor",        "frequency",    "Frequency",        kFrequency,    nullptr},
        {"sensor",        "mode",         "Mode",             kMode,         "mdi:sine-wave"},
        {"sensor",        "band",         "Band",             kBand,         "mdi:signal"},
        {"sensor",        "count_today",  "QSOs today (UTC)", kCountToday,   "mdi:counter"},
        {"sensor",        "last_qso",     "Last QSO",         kLastQso,      "mdi:account-voice"},
    };

    QVector<DiscoveryMessage> out;
    for (const Entity& e : kEntities) {
        const QString object = QString::fromLatin1(e.object);
        DiscoveryMessage m;
        m.topic = QStringLiteral("homeassistant/%1/%2/%3/config")
                      .arg(QString::fromLatin1(e.component), uidBase, object);

        const bool isLastQso = object == QLatin1String("last_qso");
        if (isLastQso && !includeQsoDetails) {
            out.append(m);                       // empty payload: remove the entity
            continue;
        }

        QJsonObject c;
        c.insert(QStringLiteral("name"), QString::fromLatin1(e.name));
        c.insert(QStringLiteral("unique_id"), uidBase + QLatin1Char('_') + object);
        c.insert(QStringLiteral("object_id"), uidBase + QLatin1Char('_') + object);
        c.insert(QStringLiteral("state_topic"),
                 topicPrefix + QLatin1Char('/') + QString::fromLatin1(e.subtopic));
        c.insert(QStringLiteral("availability_topic"), availability);
        c.insert(QStringLiteral("device"), device);
        if (e.icon) c.insert(QStringLiteral("icon"), QString::fromLatin1(e.icon));

        if (QLatin1String(e.component) == QLatin1String("binary_sensor")) {
            c.insert(QStringLiteral("payload_on"),  QStringLiteral("ON"));
            c.insert(QStringLiteral("payload_off"), QStringLiteral("OFF"));
            if (object == QLatin1String("connected"))
                c.insert(QStringLiteral("device_class"), QStringLiteral("connectivity"));
        }
        if (object == QLatin1String("frequency")) {
            c.insert(QStringLiteral("device_class"), QStringLiteral("frequency"));
            c.insert(QStringLiteral("unit_of_measurement"), QStringLiteral("Hz"));
            c.insert(QStringLiteral("suggested_unit_of_measurement"), QStringLiteral("MHz"));
            c.insert(QStringLiteral("suggested_display_precision"), 3);
        }
        if (object == QLatin1String("count_today")) {
            c.insert(QStringLiteral("state_class"), QStringLiteral("total_increasing"));
        }
        if (isLastQso) {
            c.insert(QStringLiteral("value_template"), QStringLiteral("{{ value_json.call }}"));
            c.insert(QStringLiteral("json_attributes_topic"),
                     topicPrefix + QLatin1Char('/') + QString::fromLatin1(kLastQso));
        }
        m.payload = QJsonDocument(c).toJson(QJsonDocument::Compact);
        out.append(m);
    }
    return out;
}

} // namespace ShackBook::ShackStatus
