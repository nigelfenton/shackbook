#pragma once

// WSJT-X "Secondary UDP Server (deprecated)" receiver.
//
// Modern WSJT-X exposes its log-on-QSO hook in the Reporting tab as the
// "Secondary UDP Server" — when the operator logs a QSO, WSJT-X fires a
// single UDP datagram containing the new QSO as an ADIF record at the
// configured (host, port).  Despite the name overlap with N3FJP's port
// 1100, this is NOT the N3FJP TCP/XML protocol — it's UDP carrying raw
// ADIF text.
//
// WsjtxAdifReceiver listens on a UDP port (default 1100, matching the
// TCP N3FJP server — TCP and UDP on the same port number are independent
// sockets), parses each datagram as one ADIF record, builds a Qso, and
// hands it to LogbookModel::insertQso with actor="wsjtx:<peer-ip>" so
// the audit row identifies the origin distinctly from the N3FJP TCP
// path.
//
// Parsing is tolerant: a datagram may be a bare record (just fields +
// <EOR>) or a full ADIF document (preamble + <EOH> + record + <EOR>).
// Tag names are case-insensitive per the ADIF spec; we lowercase
// everything for lookup.

#include <QObject>
#include <QUdpSocket>

namespace ShackBook {
class LogbookModel;
}

namespace ShackBook::Server {

class WsjtxAdifReceiver : public QObject {
    Q_OBJECT

public:
    explicit WsjtxAdifReceiver(LogbookModel* model, QObject* parent = nullptr);

    bool start(quint16 port = 1100);

    quint16 port() const;
    bool    isListening() const;

private slots:
    void onReadyRead();

private:
    LogbookModel* m_model;     // not owned
    QUdpSocket*   m_socket;
};

} // namespace ShackBook::Server
