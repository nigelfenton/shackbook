#include "N1mmSpotClient.h"

#include "N1mmSpotParser.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QUdpSocket>

namespace ShackBook {

namespace {
// One spot is a few hundred bytes. Anything bigger is not a spot, and reading
// it whole would only cost memory.
constexpr qint64 kMaxDatagram = 64 * 1024;
}

N1mmSpotClient::N1mmSpotClient(QObject* parent) : QObject(parent) {}

N1mmSpotClient::~N1mmSpotClient()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
    }
}

bool N1mmSpotClient::listening() const
{
    return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

bool N1mmSpotClient::start(quint16 port)
{
    stop();
    m_port = port;
    m_spots = 0;
    m_malformed = 0;
    m_lastError.clear();

    m_socket = new QUdpSocket(this);
    // Exclusive on purpose: see the header. A shared bind would split spots
    // between ShackBook and whatever else holds the port, silently.
    if (!m_socket->bind(QHostAddress::AnyIPv4, port, QAbstractSocket::DontShareAddress)) {
        m_lastError = tr("Can't listen for N1MM spots on UDP port %1: %2. Another program "
                         "(often SmartSDR CAT) may have it. Choose another port in "
                         "Settings and add it to N1MM's broadcast destinations.")
                          .arg(port).arg(m_socket->errorString());
        m_socket->deleteLater();
        m_socket = nullptr;
        emit listeningChanged(false, m_lastError);
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &N1mmSpotClient::onReadyRead);
    emit listeningChanged(true, QString());
    return true;
}

void N1mmSpotClient::stop()
{
    if (!m_socket) return;
    m_socket->disconnect(this);
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
    emit listeningChanged(false, QString());
}

void N1mmSpotClient::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram d = m_socket->receiveDatagram(kMaxDatagram);
        N1mmSpot parsed;
        switch (parseN1mmSpot(d.data(), parsed)) {
        case N1mmParse::NotSpot:
            break;              // RadioInfo, contactinfo, ...: expected, not ours
        case N1mmParse::Malformed:
            ++m_malformed;
            break;
        case N1mmParse::Spot:
            ++m_spots;
            if (parsed.remove) {
                emit spotDeleted(parsed.spot.call, parsed.spot.freqMhz);
            } else {
                parsed.spot.receivedAt = QDateTime::currentDateTimeUtc();
                emit spotReceived(parsed.spot);
            }
            break;
        }
    }
}

} // namespace ShackBook
