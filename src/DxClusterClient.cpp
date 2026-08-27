#include "DxClusterClient.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace ShackBook {

namespace {
constexpr int kBackoff[] = { 1, 2, 5, 10, 30 };
constexpr int kBackoffMaxIndex =
    static_cast<int>(sizeof(kBackoff) / sizeof(kBackoff[0])) - 1;
int backoffSeconds(int attempt) {
    if (attempt < 0) attempt = 0;
    return kBackoff[attempt < kBackoffMaxIndex ? attempt : kBackoffMaxIndex];
}

// "DX de SPOTTER[suffix]:    14250.0  K1ABC      CQ DX            1234Z"
// Fields:
//   1: spotter (with optional -# suffix)
//   2: freq in kHz (decimal optional)
//   3: spotted callsign
//   4: comment (rest of line, trimmed of trailing time)
//   5: time HHMMZ (optional)
const QRegularExpression kSpotRe{
    R"(^DX\s+de\s+(\S+):\s+([\d.]+)\s+(\S+)\s*(.*?)\s*(?:(\d{4})Z)?\s*$)",
    QRegularExpression::CaseInsensitiveOption
};
} // namespace

DxClusterClient::DxClusterClient(QObject* parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_reconnectTimer(new QTimer(this)),
      m_loginTimer(new QTimer(this)),
      m_stableTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    m_loginTimer->setSingleShot(true);
    m_stableTimer->setSingleShot(true);

    // Only a connection that survives this long counts as healthy enough
    // to reset the backoff ladder.  An accept-then-drop server (or a
    // duplicate-login ping-pong) keeps climbing toward the 30 s cap.
    m_stableTimer->setInterval(45 * 1000);
    connect(m_stableTimer, &QTimer::timeout, this, [this]() {
        m_reconnectAttempts = 0;
    });

    connect(m_socket, &QTcpSocket::connected,    this, &DxClusterClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &DxClusterClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &DxClusterClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &DxClusterClient::onErrorOccurred);

    connect(m_reconnectTimer, &QTimer::timeout, this, &DxClusterClient::onReconnectTimeout);

    // Fallback: if 3 s after connect we've still not seen anything that
    // looks like a login prompt, send our login anyway.  Some clusters
    // greet briefly and start streaming without explicit prompting.
    connect(m_loginTimer, &QTimer::timeout, this, [this]() {
        if (m_connected && !m_loginSent) {
            m_socket->write((m_callsign + m_loginSuffix + "\r\n").toUtf8());
            m_loginSent = true;
        }
    });
}

DxClusterClient::~DxClusterClient()
{
    cancelReconnect();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void DxClusterClient::connectToCluster(const QString& host, quint16 port,
                                       const QString& callsign,
                                       const QString& loginSuffix)
{
    m_userInitiatedDisconnect = false;
    m_reconnectAttempts = 0;
    m_host = host;
    m_port = port;
    m_callsign = callsign.trimmed().toUpper();
    m_loginSuffix = loginSuffix.trimmed();
    m_loginSent = false;
    m_rxBuffer.clear();

    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
    m_socket->connectToHost(host, port);
}

void DxClusterClient::disconnectFromCluster()
{
    m_userInitiatedDisconnect = true;
    cancelReconnect();
    m_loginTimer->stop();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
    setConnected(false);
}

void DxClusterClient::onConnected()
{
    m_loginSent = false;
    m_rxBuffer.clear();
    setConnected(true);
    m_loginTimer->start(3000);            // grace window for prompt-driven login
    m_stableTimer->start();               // backoff resets only after 45 s up
}

void DxClusterClient::onDisconnected()
{
    m_loginTimer->stop();
    m_stableTimer->stop();
    setConnected(false);
    if (!m_userInitiatedDisconnect) scheduleReconnect();
}

void DxClusterClient::onErrorOccurred()
{
    if (m_socket) m_lastError = m_socket->errorString();
}

void DxClusterClient::onReadyRead()
{
    m_rxBuffer.append(m_socket->readAll());

    // Process complete lines (\n-terminated).  Cluster nodes vary
    // between \r\n and \n line endings; QString::trimmed() flattens both.
    while (true) {
        const int nl = m_rxBuffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray rawBytes = m_rxBuffer.left(nl);
        m_rxBuffer.remove(0, nl + 1);
        const QString line = QString::fromLatin1(rawBytes).trimmed();
        if (!line.isEmpty()) {
            emit rawLine(line);
            // maybeSendLogin handles BOTH the "send login on prompt" and
            // "abort on login-rejection" cases — keep it firing for every
            // line so we can detect rejections that arrive after the
            // login was sent.
            maybeSendLogin(line);
            parseLine(line);
        }
    }

    // Some clusters prompt for the login WITHOUT a newline ("login: " on
    // the wire) — check the partial buffer too.
    if (!m_loginSent) {
        const QString partial = QString::fromLatin1(m_rxBuffer).trimmed();
        if (!partial.isEmpty()) maybeSendLogin(partial);
    }
}

void DxClusterClient::maybeSendLogin(const QString& chunk)
{
    const QString lower = chunk.toLower();

    // DXSpider duplicate-login kick — a NEW client logged in with our
    // exact callsign-SSID and the server is dropping US in its favour:
    //   "Reconnected as G0JKN-2 at 1.2.3.4, this instance is disconnected"
    // Storming straight back in would kick the other client, which storms
    // back in turn — two clients sharing a login ping-pong at ~1 s forever
    // (proven live against NG7M-1, 2026-06-12).  Go to max backoff and
    // surface it so the operator can find the second client.
    if (m_loginSent && lower.contains("this instance is disconnected")) {
        m_lastError = chunk.trimmed();
        m_reconnectAttempts = kBackoffMaxIndex;
        emit duplicateLoginKick(chunk.trimmed());
        return;
    }

    // Catch a login-rejection message before we re-send the login on
    // the same prompt.  DXSpider answers "login: Sorry G0JKN-L is an
    // invalid callsign" and re-prompts; without this guard we'd loop
    // forever bouncing between connect → reject → reconnect.
    if (m_loginSent && (lower.contains("invalid callsign") ||
                        lower.contains("invalid login")    ||
                        lower.contains("sorry"))) {
        const QString reason = chunk.trimmed();
        m_lastError = reason;
        m_userInitiatedDisconnect = true;     // stop the auto-reconnect spam
        cancelReconnect();
        emit loginRejected(reason);
        if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->disconnectFromHost();
        return;
    }

    // Only fire the login-send once — once m_loginSent is true the
    // cluster has already accepted us (or is about to reject us via
    // the branch above), so any later line that happens to mention
    // "login" or "callsign" (the welcome banner, user list, sysop
    // info, etc.) MUST NOT trigger a re-send.  Doing so generates
    // "Cmd too long or has invalid characters" responses on DXSpider.
    if (!m_loginSent && (lower.contains("login")    ||
                         lower.contains("callsign") ||
                         lower.contains("call:")    ||
                         lower.contains("your call"))) {
        m_socket->write((m_callsign + m_loginSuffix + "\r\n").toUtf8());
        m_loginSent = true;
        m_loginTimer->stop();
    }
}

void DxClusterClient::parseLine(const QString& line)
{
    const auto match = kSpotRe.match(line);
    if (!match.hasMatch()) return;

    SpotData s;
    // const QString spotter = match.captured(1);   // available if we ever want it
    bool ok = false;
    const double freqKhz = match.captured(2).toDouble(&ok);
    if (!ok || freqKhz <= 0.0) return;
    s.freqMhz   = freqKhz / 1000.0;
    s.call      = match.captured(3).trimmed().toUpper();
    s.comment   = match.captured(4).trimmed();
    s.source    = QString("DXC: %1").arg(m_host);
    s.receivedAt = QDateTime::currentDateTimeUtc();
    s.lifetimeSec = 1800;

    if (s.call.isEmpty()) return;
    emit spotReceived(s);
}

void DxClusterClient::scheduleReconnect()
{
    if (m_userInitiatedDisconnect) return;
    const int secs = backoffSeconds(m_reconnectAttempts);
    ++m_reconnectAttempts;
    m_reconnectTimer->start(secs * 1000);
}

void DxClusterClient::cancelReconnect()
{
    if (m_reconnectTimer) m_reconnectTimer->stop();
}

void DxClusterClient::onReconnectTimeout()
{
    if (m_userInitiatedDisconnect || m_host.isEmpty()) return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void DxClusterClient::setConnected(bool c)
{
    if (m_connected == c) return;
    m_connected = c;
    emit connectionChanged(c);
}

} // namespace ShackBook
