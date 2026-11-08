#pragma once

// MqttPublisher — keeps a connection to a LAN MQTT broker and publishes
// retained shack status to it (#24).
//
// Design rules, in order of importance:
//   1. NEVER get in the way of logging. Nothing here blocks, nothing throws a
//      dialog; a dead broker means status is not published, and that is all.
//   2. State, not events. Every value is published RETAINED and remembered, so
//      a broker restart, a dashboard reload or a reconnect shows the current
//      state immediately — the whole cache is re-sent on every connect.
//   3. The broker tells Home Assistant when ShackBook disappears: the Last Will
//      sets `<prefix>/availability` to `offline` if the TCP session dies
//      without a clean DISCONNECT (crash, power cut, network loss).
//
// QoS 0 only; no subscribe; no TLS (LAN broker). See MqttPacket for the wire.

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpSocket;
class QTimer;

namespace ShackBook {

struct MqttConfig {
    bool    enabled{false};
    QString host;
    quint16 port{1883};
    QString username;
    QString password;
    QString clientId;
    QString topicPrefix;             // e.g. "shackbook/g0jkn"; no trailing '/'
    quint16 keepAliveSec{60};

    bool operator==(const MqttConfig& o) const   // C++17: no defaulted ==
    {
        return enabled == o.enabled && host == o.host && port == o.port
            && username == o.username && password == o.password
            && clientId == o.clientId && topicPrefix == o.topicPrefix
            && keepAliveSec == o.keepAliveSec;
    }
    bool operator!=(const MqttConfig& o) const { return !(*this == o); }
};

class MqttPublisher : public QObject {
    Q_OBJECT

public:
    explicit MqttPublisher(QObject* parent = nullptr);
    ~MqttPublisher() override;

    // Apply settings. Disabled: disconnect cleanly (availability -> offline)
    // and stop retrying. Enabled with a changed config: reconnect. Unchanged:
    // no-op, so it is safe to call after every Settings dialog.
    void configure(const MqttConfig& cfg);

    // Publish `payload` retained at `<prefix>/<subtopic>`. Remembered and
    // re-sent on reconnect; an identical repeat is not re-sent.
    void publishState(const QString& subtopic, const QByteArray& payload);

    // Publish retained at an ABSOLUTE topic (Home Assistant discovery lives
    // outside the prefix). Remembered like publishState. An EMPTY payload is a
    // retained delete: the broker forgets the topic, and so does the cache
    // after sending it.
    void publishAbsolute(const QString& topic, const QByteArray& payload);

    // Coalesce rapid updates to `subtopic` (e.g. frequency while spinning the
    // VFO) to at most one publish per `intervalMs`. The LAST value always
    // goes out.
    void setThrottle(const QString& subtopic, int intervalMs);

    bool    isConnected() const { return m_state == State::Connected; }
    QString lastError()   const { return m_lastError; }
    QString availabilityTopic() const;

    // Test hook: shorten the reconnect backoff (production: 1,2,5,10,30 s).
    void setBackoffScaleForTest(double scale) { m_backoffScale = scale; }

signals:
    void connectionChanged(bool connected);
    void errorChanged(const QString& error);

private:
    enum class State { Idle, Connecting, AwaitingConnack, Connected };

    void startConnect();
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError();
    void scheduleReconnect();
    void goIdle(bool sendDisconnect);
    void setError(const QString& e);
    void sendNow(const QString& topic, const QByteArray& payload);
    void flushThrottled();
    QString fullTopic(const QString& subtopic) const;

    MqttConfig  m_cfg;
    QTcpSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};
    QTimer*     m_pingTimer{nullptr};
    QTimer*     m_throttleTimer{nullptr};
    State       m_state{State::Idle};
    int         m_attempt{0};
    double      m_backoffScale{1.0};
    QByteArray  m_rx;
    QString     m_lastError;

    // Retained values by ABSOLUTE topic, in first-published order for resend.
    QHash<QString, QByteArray> m_retained;
    QStringList                m_retainedOrder;
    // Throttling: per absolute topic, interval, last send time, pending value.
    QHash<QString, int>        m_throttleMs;
    QHash<QString, qint64>     m_lastSentMs;
    QHash<QString, QByteArray> m_pending;
};

} // namespace ShackBook
