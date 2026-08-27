#include "AprsKissClient.h"

#include <QTcpSocket>
#include <QTimer>
#include <QtGlobal>

namespace ShackBook {

namespace {
// Same backoff schedule as TciClient.
int backoffSeconds(int attempt)
{
    static const int schedule[] = {1, 2, 5, 10, 30};
    const int idx = qMin(attempt, int(std::size(schedule)) - 1);
    return schedule[qMax(0, idx)];
}
constexpr int kMaxRxBuffer = 64 * 1024; // guard against an endless partial frame
} // namespace

AprsKissClient::AprsKissClient(QObject* parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &AprsKissClient::onReconnectTimeout);

    connect(m_socket, &QTcpSocket::connected,    this, &AprsKissClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &AprsKissClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &AprsKissClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &AprsKissClient::onErrorOccurred);
}

AprsKissClient::~AprsKissClient() = default;

void AprsKissClient::connectToServer(const QString& host, quint16 port)
{
    m_userInitiatedDisconnect = false;
    m_host = host;
    m_port = port;
    m_reconnectAttempts = 0;
    m_rxBuffer.clear();
    cancelReconnect();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_socket->connectToHost(host, port);
}

void AprsKissClient::disconnectFromServer()
{
    m_userInitiatedDisconnect = true;
    cancelReconnect();
    m_socket->abort();
    setConnected(false);
}

void AprsKissClient::onConnected()
{
    m_reconnectAttempts = 0;
    m_rxBuffer.clear();
    setConnected(true);
}

void AprsKissClient::onDisconnected()
{
    setConnected(false);
    if (!m_userInitiatedDisconnect)
        scheduleReconnect();
}

void AprsKissClient::onErrorOccurred()
{
    m_lastError = m_socket->errorString();
    if (m_connected)
        setConnected(false);
    if (!m_userInitiatedDisconnect)
        scheduleReconnect();
}

void AprsKissClient::onReadyRead()
{
    m_rxBuffer += m_socket->readAll();

    QByteArray leftover;
    const auto frames = Aprs::kissUnframe(m_rxBuffer, &leftover);
    m_rxBuffer = leftover;
    // Runaway guard: a stream with no FEND ever would grow unbounded.
    if (m_rxBuffer.size() > kMaxRxBuffer)
        m_rxBuffer.clear();

    for (const QByteArray& ax25 : frames) {
        emit rawFrame(ax25);
        if (auto r = Aprs::decodeAx25(ax25))
            emit aprsReport(*r);
    }
}

bool AprsKissClient::sendMessage(const QString& source, const QString& addressee,
                                 const QString& text, const QStringList& path,
                                 const QString& msgNo)
{
    const QByteArray info = Aprs::buildMessageInfo(addressee, text, msgNo);
    return sendInfo(source, QStringLiteral("APZ001"), info, path);
}

bool AprsKissClient::sendInfo(const QString& source, const QString& dest,
                              const QByteArray& info, const QStringList& path)
{
    const QByteArray ax25 = Aprs::buildAx25Ui(source, dest, path, info);
    if (ax25.isEmpty())
        return false;
    return sendAx25(ax25);
}

bool AprsKissClient::sendAx25(const QByteArray& ax25)
{
    if (!m_connected || m_socket->state() != QAbstractSocket::ConnectedState)
        return false;
    const QByteArray kiss = Aprs::kissFrameData(ax25);
    return m_socket->write(kiss) == kiss.size();
}

void AprsKissClient::scheduleReconnect()
{
    if (m_userInitiatedDisconnect || m_host.isEmpty())
        return;
    const int secs = backoffSeconds(m_reconnectAttempts++);
    m_reconnectTimer->start(secs * 1000);
}

void AprsKissClient::cancelReconnect()
{
    m_reconnectTimer->stop();
}

void AprsKissClient::onReconnectTimeout()
{
    if (m_userInitiatedDisconnect || m_host.isEmpty())
        return;
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void AprsKissClient::setConnected(bool c)
{
    if (m_connected == c)
        return;
    m_connected = c;
    emit connectionChanged(c);
}

} // namespace ShackBook
