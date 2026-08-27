#pragma once

// N3FJP-protocol-compatible TCP server.
//
// Listens on port 1100 (the N3FJP default).  Speaks the N3FJP TCP/XML
// command envelope per the cached spec at:
//   \\MYCLOUDEX2ULTRA\nigel\documents\_claude\api\n3fjp\n3fjp-api-spec-2026-05-20.md
//
// Identifies as "ShackBook" with an explicit "N3FJP-compat-<X>" label —
// NEVER as N3FJP itself.  Clean-room from the public spec only.  See
// design doc §13 ethical posture.
//
// Phase 1c implemented commands:
//   <PROGRAM>         — identity
//   <APIVER>          — API version
//   <UPDATEANDLOG>    — log a QSO (the WSJT-X path)
//
// Phase 1d will add: <SETUPDATESTATE>, <ENTEREVENT> push, <READBMF>,
// <QSOINPROGRESS>, <GETCALLQTHINFO>, <DUPECHECK>.

#include <QObject>
#include <QString>
#include <QTcpServer>

class QTcpSocket;

namespace ShackBook {
class LogbookModel;
}

namespace ShackBook::Server {

class N3fjpServer : public QObject {
    Q_OBJECT

public:
    explicit N3fjpServer(LogbookModel* model, QObject* parent = nullptr);

    bool start(quint16 port = 1100);

    quint16 port() const;
    bool    isListening() const;

private slots:
    void onNewConnection();
    void onSocketReadyRead();

private:
    // Parse + dispatch one CR+LF-terminated frame.  Returns the response
    // to send (empty == no response, e.g. silent success on UPDATEANDLOG).
    QString handleFrame(const QString& frame, const QString& clientId);

    // Per-command handlers (return the response envelope or empty).
    QString handleUpdateAndLog(const QString& inner, const QString& clientId);

    LogbookModel* m_model;     // not owned
    QTcpServer*   m_server;
};

} // namespace ShackBook::Server
