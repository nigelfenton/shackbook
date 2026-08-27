#pragma once

// N3FJP-protocol CLIENT — connects OUT to a remote N3FJP server (e.g. an
// N3FJP Field Day Contest Log on a club logging laptop) and MIRRORS its QSOs
// into the ShackBook logbook as they are entered.
//
// This is the inverse of N3fjpServer (which listens).  On connect it sends
//   <CMD><SETUPDATESTATE><VALUE>TRUE</VALUE></CMD>
// to subscribe, then parses the <ENTEREVENT> frames N3FJP pushes when an
// operator logs a QSO and inserts each into the shared LogbookModel — so the
// HTTP API (/api/qsos) and anything downstream (the FD map) see the live log.
//
// ENTEREVENT wire format captured live against FieldDay 6.6.10 / API 2.2
// (2026-06-22) — see ~/Documents/Claude/n3fjp-capture/ENTEREVENT-format.md:
//   <CMD><ENTEREVENT><QSOCOUNT>n</QSOCOUNT><CALL>..</CALL><BAND>20</BAND>
//     <MODE>SSB</MODE><MODETEST>PH</MODETEST><FREQ>..</FREQ><ARRL_SECT>..</ARRL_SECT>
//     <CLASS>..</CLASS><QSO_DATE>YYYYMMDD</QSO_DATE><TIME_ON>HHMMSS</TIME_ON>
//     <RST_SENT>..</RST_SENT><RST_RCVD>..</RST_RCVD>...</CMD>
//
// Auto-reconnects (Field-Day reliability): if the link drops it keeps retrying.

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QAbstractSocket>

class QTcpSocket;
class QTimer;

namespace ShackBook {
class LogbookModel;
}

namespace ShackBook::Server {

class N3fjpClient : public QObject {
    Q_OBJECT

public:
    explicit N3fjpClient(LogbookModel* model, QObject* parent = nullptr);

    // Begin connecting to the remote N3FJP server and keep the link up
    // (reconnecting on drop) until stop().
    void start(const QString& host, quint16 port = 1100);
    void stop();

    bool isConnected() const;
    int  mirroredCount() const { return m_mirrored; }

signals:
    void connectedChanged(bool connected);
    void qsoMirrored(qint64 id, const QString& call);  // diagnostic

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError);
    void tryReconnect();

private:
    void handleFrame(const QString& frame);   // one <CMD>...</CMD> envelope
    void handleEnterEvent(const QString& inner);

    LogbookModel* m_model;          // not owned
    QTcpSocket*   m_socket;
    QTimer*       m_reconnect;
    QString       m_host;
    quint16       m_port{1100};
    QByteArray    m_buf;            // frame reassembly
    bool          m_running{false};
    int           m_mirrored{0};
};

} // namespace ShackBook::Server
