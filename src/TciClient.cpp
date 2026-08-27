#include "TciClient.h"

#include <QWebSocket>
#include <QTimer>
#include <QStringList>
#include <QDebug>

namespace ShackBook {

namespace {

// Backoff schedule (seconds) for auto-reconnect.  Last value repeats.
constexpr int kBackoff[] = { 1, 2, 5, 10, 30 };

int backoffSeconds(int attempt)
{
    const int n = static_cast<int>(sizeof(kBackoff) / sizeof(kBackoff[0]));
    if (attempt < 0) attempt = 0;
    return kBackoff[attempt < n ? attempt : n - 1];
}

} // namespace

TciClient::TciClient(QObject* parent)
    : QObject(parent),
      m_socket(new QWebSocket(QString{}, QWebSocketProtocol::VersionLatest, this)),
      m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QWebSocket::connected,        this, &TciClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,     this, &TciClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &TciClient::onTextMessage);
    // QWebSocket grew errorOccurred in Qt 6.5; distro Qt 6.4 (Ubuntu 24.04)
    // still has only the overloaded error() signal.  Either way we only need
    // a notification, not the specific code.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_socket, &QWebSocket::errorOccurred,
            this, &TciClient::onErrorOccurred);
#else
    connect(m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &TciClient::onErrorOccurred);
#endif

    connect(m_reconnectTimer, &QTimer::timeout,
            this, &TciClient::onReconnectTimeout);
}

TciClient::~TciClient()
{
    cancelReconnect();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void TciClient::connectToServer(const QString& host, quint16 port)
{
    m_userInitiatedDisconnect = false;
    m_reconnectAttempts = 0;

    QUrl url;
    url.setScheme("ws");
    url.setHost(host);
    url.setPort(port);
    m_url = url;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->open(m_url);
}

void TciClient::disconnectFromServer()
{
    m_userInitiatedDisconnect = true;
    cancelReconnect();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    }
    setConnected(false);
}

void TciClient::send(const QString& cmd)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->sendTextMessage(cmd);
    }
}

void TciClient::onConnected()
{
    m_reconnectAttempts = 0;
    setConnected(true);
    // TCI dialect varies between servers — sending `start;` is enough for
    // AetherSDR, ExpertSDR2, and SunSDR-mb1.  Servers ignore unknown
    // commands.  We do NOT also send `iq_start;` etc — we don't want IQ.
    send("start;");
}

void TciClient::onDisconnected()
{
    // Forget the announced device. Without this a QSO logged after switching
    // to a different radio would be stamped with the PREVIOUS radio's name —
    // a wrong attribution is worse than a blank one.
    if (!m_device.isEmpty()) {
        m_device.clear();
        emit deviceNameChanged(m_device);
    }
    setConnected(false);
    if (!m_userInitiatedDisconnect) {
        scheduleReconnect();
    }
}

void TciClient::onErrorOccurred()
{
    if (m_socket) m_lastError = m_socket->errorString();
}

void TciClient::onTextMessage(const QString& message)
{
    // Some TCI servers concatenate multiple commands per WebSocket frame
    // (`vfo:0,0,14250000;mode:0,USB;`); split on ';' to handle both forms.
    const QStringList lines = message.split(';', Qt::SkipEmptyParts);
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        emit rawMessageReceived(line);
        parseLine(line);
    }
}

void TciClient::parseLine(const QString& line)
{
    // Format: command:arg1,arg2,arg3
    const int colon = line.indexOf(':');
    QString cmd  = colon < 0 ? line : line.left(colon);
    QString rest = colon < 0 ? QString{} : line.mid(colon + 1);
    cmd = cmd.trimmed().toLower();
    const QStringList args = rest.split(',', Qt::KeepEmptyParts);

    if (cmd == "vfo" && args.size() >= 3) {
        // vfo:rx,vfo,freqHz — track RX 0 / VFO 0 only.
        if (args[0].trimmed() == "0" && args[1].trimmed() == "0") {
            const qint64 hz = args[2].trimmed().toLongLong();
            const double mhz = hz / 1.0e6;
            if (qAbs(mhz - m_freqMhz) > 1.0e-9) {
                m_freqMhz = mhz;
                emit frequencyChanged(m_freqMhz);
            }
        }
    } else if ((cmd == "mode" || cmd == "modulation") && args.size() >= 2) {
        // mode:rx,modeString — track RX 0 only.
        // AetherSDR sends `modulation:` instead of `mode:` (with the same
        // arg shape and a lowercase mode string like "lsb"); accept both
        // so a single TciClient works against AetherSDR, ExpertSDR2, and
        // SunSDR-mb1 without per-server branching.
        if (args[0].trimmed() == "0") {
            const QString m = args[1].trimmed().toUpper();
            if (m != m_mode) {
                m_mode = m;
                emit modeChanged(m_mode);
            }
        }
    } else if (cmd == "protocol" && args.size() >= 2) {
        m_protoName    = args[0].trimmed();
        m_protoVersion = args[1].trimmed();
        emit serverInfoChanged(m_protoName, m_protoVersion);
    } else if (cmd == "device" && args.size() >= 1) {
        // device:<name> — announced once on connect. Identifies the SERVER
        // APPLICATION (e.g. "AetherSDR"), not the radio behind it.
        const QString d = args[0].trimmed();
        if (!d.isEmpty() && d != m_device) {
            m_device = d;
            emit deviceNameChanged(m_device);
        }
    } else if (cmd == "if" && args.size() >= 3) {
        // Some servers send `if:` instead of `vfo:` for the RX freq.
        if (args[0].trimmed() == "0" && args[1].trimmed() == "0") {
            const qint64 hz = args[2].trimmed().toLongLong();
            const double mhz = hz / 1.0e6;
            // `if` is a fallback — only honour it if we have nothing yet.
            if (m_freqMhz <= 0.0 && mhz > 0.0) {
                m_freqMhz = mhz;
                emit frequencyChanged(m_freqMhz);
            }
        }
    }
    // Other events (trx, drive, rit, xit, sql, ...) intentionally ignored
    // — we only need freq + mode for the logbook.
}

void TciClient::scheduleReconnect()
{
    if (m_userInitiatedDisconnect) return;
    if (m_probeMode) return;   // discovery probes are one-shot by design
    const int secs = backoffSeconds(m_reconnectAttempts);
    ++m_reconnectAttempts;
    m_reconnectTimer->start(secs * 1000);
}

void TciClient::cancelReconnect()
{
    if (m_reconnectTimer) m_reconnectTimer->stop();
}

void TciClient::onReconnectTimeout()
{
    if (m_userInitiatedDisconnect || m_url.isEmpty()) return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->open(m_url);
}

void TciClient::setConnected(bool c)
{
    if (m_connected == c) return;
    m_connected = c;
    emit connectionChanged(c);
}

} // namespace ShackBook
