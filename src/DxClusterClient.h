#pragma once

// DxClusterClient — simple telnet client for DX cluster nodes.
//
// Connects to host:port over TCP, sends `<callsign>-L` as the login
// (the conventional "logger" suffix that lets us coexist with the
// operator's primary cluster session), then parses incoming spot
// lines into SpotData objects.
//
// Supported spot-line forms (matched leniently):
//   DX de N1XX:    14250.0  K1ABC      CQ DX                    1234Z
//   DX de N1XX-#:  14250.0  K1ABC/P    POTA K-0123              1234Z
//
// The freq field is kHz with optional decimal (kHz.fff).  Comments
// (between call and time) are passed through verbatim.
//
// On disconnect the client auto-reconnects with the same exponential
// backoff schedule as TciClient (1, 2, 5, 10, 30 seconds).  The attempt
// counter resets only after a connection has stayed up for a while —
// resetting on raw TCP connect would turn a server that accepts-then-
// drops into a 1-second hammer loop.
//
// DXSpider allows one session per callsign-SSID: a NEW login wins and
// the existing session is told "Reconnected as <call> at <ip>, this
// instance is disconnected" and dropped.  Two clients sharing a login
// therefore kick each other forever.  We detect that message, emit
// duplicateLoginKick() so the UI can tell the operator, and jump
// straight to the maximum backoff instead of storming back.

#include "SpotData.h"

#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace ShackBook {

class DxClusterClient : public QObject {
    Q_OBJECT
public:
    explicit DxClusterClient(QObject* parent = nullptr);
    ~DxClusterClient() override;

    // The login is sent as <callsign><loginSuffix>.  Common values:
    //   "-2" — DXSpider's preferred secondary-station SSID (default)
    //   ""   — bare callsign (for clusters with strict regex; risks
    //          kicking the operator's primary session)
    //   "-L" — CC Cluster / AR-Cluster "logger" suffix (rejected by DXSpider)
    void connectToCluster(const QString& host, quint16 port,
                          const QString& callsign,
                          const QString& loginSuffix = QStringLiteral("-2"));
    void disconnectFromCluster();

    bool connected() const { return m_connected; }
    QString lastError() const { return m_lastError; }

signals:
    void connectionChanged(bool connected);
    void spotReceived(const ShackBook::SpotData& spot);
    void rawLine(const QString& line);            // diagnostic
    void loginRejected(const QString& reason);    // server refused our callsign
    // Another client logged in with our exact callsign-SSID and the
    // server dropped us in its favour. Reconnect continues, but slowly.
    void duplicateLoginKick(const QString& serverMessage);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred();
    void onReconnectTimeout();

private:
    void scheduleReconnect();
    void cancelReconnect();
    void setConnected(bool c);
    void parseLine(const QString& line);
    void maybeSendLogin(const QString& chunk);

    QTcpSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};
    QTimer*     m_loginTimer{nullptr};
    QTimer*     m_stableTimer{nullptr};   // resets backoff after sustained uptime

    QString  m_host;
    quint16  m_port{0};
    QString  m_callsign;             // bare callsign
    QString  m_loginSuffix{"-2"};    // appended at login
    bool     m_loginSent{false};
    bool     m_userInitiatedDisconnect{false};
    bool     m_connected{false};
    int      m_reconnectAttempts{0};
    QString  m_lastError;

    QByteArray m_rxBuffer;           // accumulator for partial lines
};

} // namespace ShackBook
