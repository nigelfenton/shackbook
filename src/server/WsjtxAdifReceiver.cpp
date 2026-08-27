#include "WsjtxAdifReceiver.h"

#include "AdifReader.h"
#include "LogbookModel.h"
#include "Qso.h"

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QHostAddress>
#include <QString>

namespace ShackBook::Server {



// ───────────────────────── WsjtxAdifReceiver ─────────────────────────

WsjtxAdifReceiver::WsjtxAdifReceiver(LogbookModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_socket(new QUdpSocket(this))
{
    connect(m_socket, &QUdpSocket::readyRead,
            this,     &WsjtxAdifReceiver::onReadyRead);
}

bool WsjtxAdifReceiver::start(quint16 port)
{
    // ShareAddress so we coexist gracefully with anything else that may
    // ever want UDP on the same port (e.g. multiple loggers behind a
    // wireguard mesh, etc.).
    const bool ok = m_socket->bind(QHostAddress::Any, port,
                                   QUdpSocket::ShareAddress |
                                   QUdpSocket::ReuseAddressHint);
    if (!ok) {
        qWarning() << "WsjtxAdifReceiver: bind UDP" << port << "failed:"
                   << m_socket->errorString();
        return false;
    }
    qInfo() << "WsjtxAdifReceiver: listening on UDP port"
            << m_socket->localPort();
    return true;
}

quint16 WsjtxAdifReceiver::port() const
{
    return m_socket->localPort();
}

bool WsjtxAdifReceiver::isListening() const
{
    return m_socket->state() == QAbstractSocket::BoundState;
}

void WsjtxAdifReceiver::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray   buf(m_socket->pendingDatagramSize(), Qt::Uninitialized);
        QHostAddress peer;
        quint16      peerPort = 0;
        const qint64 n = m_socket->readDatagram(
            buf.data(), buf.size(), &peer, &peerPort);
        if (n <= 0) continue;
        buf.resize(static_cast<int>(n));

        const QString peerStr = peer.toString();
        qInfo().noquote()
            << QString("WsjtxAdifReceiver: %1 bytes from %2:%3")
                   .arg(n).arg(peerStr).arg(peerPort);

        // Parse via the shared AdifReader (nextRecord + qsoFromFields) rather
        // than a private copy — qsoFromFields also folds MODE:USB/LSB -> SSB
        // and derives band-from-freq. A WSJT-X datagram is one QSO, but loop
        // the multi-record iterator anyway so a batched datagram is handled.
        qsizetype pos = 0;
        QHash<QString, QString> fields;
        int recordsInDatagram = 0;
        while (Adif::nextRecord(buf, pos, &fields)) {
            ++recordsInDatagram;
            Qso q = Adif::qsoFromFields(fields);

            QStringList missing;
            if (q.call.isEmpty())    missing << "CALL";
            if (q.qsoDate.isEmpty()) missing << "QSO_DATE";
            if (q.timeOn.isEmpty())  missing << "TIME_ON";
            if (q.band.isEmpty())    missing << "BAND";
            if (q.mode.isEmpty())    missing << "MODE";
            if (!missing.isEmpty()) {
                qWarning().noquote()
                    << "WsjtxAdifReceiver: ADIF missing required field(s):"
                    << missing.join(", ") << "(dropped)";
                continue;
            }

            if (!m_model || !m_model->isOpen()) {
                qWarning() << "WsjtxAdifReceiver: logbook not open, dropped";
                continue;
            }

            const QString actor = QStringLiteral("wsjtx:%1").arg(peerStr);
            if (!m_model->insertQso(q, actor)) {
                qWarning().noquote()
                    << "WsjtxAdifReceiver: insert failed —" << m_model->errorString();
                continue;
            }
            qInfo().noquote()
                << QString("WsjtxAdifReceiver: logged id=%1 call=%2 band=%3 mode=%4 actor=%5")
                       .arg(q.id).arg(q.call, q.band, q.mode, actor);
        }   // end per-record loop

        if (recordsInDatagram == 0)
            qWarning() << "WsjtxAdifReceiver: empty / unparseable datagram, dropped";
    }
}

} // namespace ShackBook::Server
