#include "MqttPublisher.h"

#include "MqttPacket.h"

#include <QDateTime>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace ShackBook {

namespace {

constexpr int kBackoffSec[] = {1, 2, 5, 10, 30};

qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

} // namespace

MqttPublisher::MqttPublisher(QObject* parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_reconnectTimer(new QTimer(this)),
      m_pingTimer(new QTimer(this)),
      m_throttleTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    m_throttleTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected,    this, &MqttPublisher::onSocketConnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &MqttPublisher::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &MqttPublisher::onSocketDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &MqttPublisher::onSocketError);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttPublisher::startConnect);
    connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Connected) m_socket->write(Mqtt::pingReqPacket());
    });
    connect(m_throttleTimer, &QTimer::timeout, this, &MqttPublisher::flushThrottled);
}

MqttPublisher::~MqttPublisher()
{
    // Best effort: a clean DISCONNECT would suppress the Last Will, so publish
    // offline explicitly first — closing the app is a real "offline".
    if (m_state == State::Connected) {
        sendNow(availabilityTopic(), QByteArrayLiteral("offline"));
        m_socket->write(Mqtt::disconnectPacket());
        m_socket->flush();
    }
}

QString MqttPublisher::fullTopic(const QString& subtopic) const
{
    return m_cfg.topicPrefix + QLatin1Char('/') + subtopic;
}

QString MqttPublisher::availabilityTopic() const
{
    return fullTopic(QStringLiteral("availability"));
}

void MqttPublisher::configure(const MqttConfig& cfg)
{
    const bool usable = cfg.enabled && !cfg.host.trimmed().isEmpty()
                        && !cfg.topicPrefix.trimmed().isEmpty();
    if (!usable) {
        goIdle(/*sendDisconnect*/ true);
        m_cfg = cfg;
        setError(cfg.enabled ? QStringLiteral("MQTT enabled but broker host or topic prefix is empty")
                             : QString{});
        return;
    }
    if (cfg == m_cfg && m_state != State::Idle) return;   // nothing to do

    const bool prefixChanged = cfg.topicPrefix != m_cfg.topicPrefix;
    goIdle(/*sendDisconnect*/ true);
    if (prefixChanged) {
        // Cached values were published under the old prefix; keep the
        // absolute (discovery) topics, drop the rest. Callers re-publish.
        const QString oldPrefix = m_cfg.topicPrefix + QLatin1Char('/');
        for (int i = m_retainedOrder.size() - 1; i >= 0; --i) {
            if (!m_cfg.topicPrefix.isEmpty() && m_retainedOrder[i].startsWith(oldPrefix)) {
                m_retained.remove(m_retainedOrder[i]);
                m_retainedOrder.removeAt(i);
            }
        }
    }
    m_cfg = cfg;
    m_attempt = 0;
    startConnect();
}

void MqttPublisher::startConnect()
{
    if (!m_cfg.enabled) return;
    // Abort BEFORE leaving Idle: aborting a live socket emits disconnected(),
    // which in any other state would schedule a second, overlapping attempt.
    m_state = State::Idle;
    m_socket->abort();
    m_rx.clear();
    m_state = State::Connecting;
    m_socket->connectToHost(m_cfg.host.trimmed(), m_cfg.port);
}

void MqttPublisher::onSocketConnected()
{
    Mqtt::ConnectOptions o;
    o.clientId     = m_cfg.clientId;
    o.username     = m_cfg.username;
    o.password     = m_cfg.password;
    o.keepAliveSec = m_cfg.keepAliveSec;
    o.willTopic    = availabilityTopic();
    o.willPayload  = QByteArrayLiteral("offline");
    o.willRetain   = true;
    m_state = State::AwaitingConnack;
    m_socket->write(Mqtt::connectPacket(o));
}

void MqttPublisher::onSocketReadyRead()
{
    m_rx.append(m_socket->readAll());
    if (m_state == State::AwaitingConnack) {
        int used = 0;
        const auto rc = Mqtt::parseConnack(m_rx, &used);
        if (!rc) {
            if (m_rx.size() >= 4) {   // something, but not a CONNACK
                setError(QStringLiteral("unexpected reply from broker (not MQTT?)"));
                m_socket->abort();
            }
            return;
        }
        m_rx.remove(0, used);
        if (*rc != 0) {
            // Wrong credentials will not fix themselves; retrying fast would
            // just hammer the broker's auth log. The normal backoff still
            // applies so a broker that was mid-restart recovers.
            setError(Mqtt::connackText(*rc));
            m_socket->abort();
            return;
        }
        m_state   = State::Connected;
        m_attempt = 0;
        setError({});
        m_pingTimer->start(qMax(5, m_cfg.keepAliveSec / 2) * 1000);

        sendNow(availabilityTopic(), QByteArrayLiteral("online"));
        for (const QString& topic : std::as_const(m_retainedOrder))
            sendNow(topic, m_retained.value(topic));
        emit connectionChanged(true);
        return;
    }
    // Connected: the only thing a publish-only QoS 0 client receives is
    // PINGRESP (d0 00). Discard whatever arrives.
    m_rx.clear();
}

void MqttPublisher::onSocketDisconnected()
{
    const bool wasConnected = m_state == State::Connected;
    m_pingTimer->stop();
    if (m_state == State::Idle) return;
    m_state = State::Connecting;
    if (wasConnected) emit connectionChanged(false);
    scheduleReconnect();
}

void MqttPublisher::onSocketError()
{
    if (m_state == State::Idle) return;
    if (m_lastError.isEmpty() || m_state != State::AwaitingConnack)
        setError(m_socket->errorString());
    // A refused or unreachable host never emits disconnected().
    if (m_socket->state() == QAbstractSocket::UnconnectedState
        && m_state != State::Connected) {
        scheduleReconnect();
    }
}

void MqttPublisher::scheduleReconnect()
{
    if (!m_cfg.enabled || m_reconnectTimer->isActive()) return;
    const int n = static_cast<int>(sizeof(kBackoffSec) / sizeof(kBackoffSec[0]));
    const int secs = kBackoffSec[qMin(m_attempt, n - 1)];
    ++m_attempt;
    m_reconnectTimer->start(qMax(1, static_cast<int>(secs * 1000 * m_backoffScale)));
}

void MqttPublisher::goIdle(bool sendDisconnect)
{
    m_reconnectTimer->stop();
    m_pingTimer->stop();
    const bool wasConnected = m_state == State::Connected;
    if (wasConnected && sendDisconnect) {
        // Must go out while still Connected (sendNow checks), and explicitly:
        // a clean DISCONNECT suppresses the Last Will, so without this the
        // availability topic would keep saying "online".
        sendNow(availabilityTopic(), QByteArrayLiteral("offline"));
        m_socket->write(Mqtt::disconnectPacket());
        m_socket->flush();
        m_state = State::Idle;
        m_socket->disconnectFromHost();
    } else {
        m_state = State::Idle;
        m_socket->abort();
    }
    if (wasConnected) emit connectionChanged(false);
}

void MqttPublisher::setError(const QString& e)
{
    if (e == m_lastError) return;
    m_lastError = e;
    emit errorChanged(e);
}

void MqttPublisher::sendNow(const QString& topic, const QByteArray& payload)
{
    if (m_state != State::Connected) return;
    m_socket->write(Mqtt::publishPacket(topic, payload, /*retain*/ true));
    m_lastSentMs.insert(topic, nowMs());
}

void MqttPublisher::setThrottle(const QString& subtopic, int intervalMs)
{
    m_throttleMs.insert(fullTopic(subtopic), qMax(0, intervalMs));
}

void MqttPublisher::publishState(const QString& subtopic, const QByteArray& payload)
{
    if (m_cfg.topicPrefix.isEmpty()) return;
    publishAbsolute(fullTopic(subtopic), payload);
}

void MqttPublisher::publishAbsolute(const QString& topic, const QByteArray& payload)
{
    const auto it = m_retained.constFind(topic);
    if (it != m_retained.constEnd() && *it == payload && !payload.isEmpty()) return;

    if (payload.isEmpty()) {
        // Retained delete: send it once, then forget the topic entirely.
        m_retained.remove(topic);
        m_retainedOrder.removeAll(topic);
        m_pending.remove(topic);
        sendNow(topic, payload);
        return;
    }

    if (it == m_retained.constEnd()) m_retainedOrder.append(topic);
    m_retained.insert(topic, payload);

    const int interval = m_throttleMs.value(topic, 0);
    if (interval > 0 && m_state == State::Connected) {
        const qint64 since = nowMs() - m_lastSentMs.value(topic, 0);
        if (since < interval) {
            m_pending.insert(topic, payload);
            if (!m_throttleTimer->isActive())
                m_throttleTimer->start(static_cast<int>(interval - since));
            return;
        }
    }
    m_pending.remove(topic);
    sendNow(topic, payload);
}

void MqttPublisher::flushThrottled()
{
    const auto pending = m_pending;
    m_pending.clear();
    for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        sendNow(it.key(), it.value());
}

} // namespace ShackBook
