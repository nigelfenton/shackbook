#pragma once

// AprsKissClient — connects to a KISS-over-TCP TNC (AetherSDR's on port 8001)
// and turns the raw AX.25 frames it streams into decoded APRS reports.
//
// KISS is a binary framing (FEND-delimited), so unlike TciClient (WebSocket
// text) this is a raw QTcpSocket. Incoming bytes are buffered and run through
// AprsDecode::kissUnframe -> decodeAx25; each decoded frame is emitted as an
// aprsReport(). The TX path (sendMessage) builds an AX.25 UI frame and writes
// it back as a KISS data frame — AetherSDR then transmits it.
//
// Auto-reconnect mirrors TciClient: on any unexpected close, retry with a
// 1/2/5/10/30 s backoff until disconnectFromServer() is called.

#include "AprsDecode.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace ShackBook {

class AprsKissClient : public QObject {
    Q_OBJECT

public:
    explicit AprsKissClient(QObject* parent = nullptr);
    ~AprsKissClient() override;

    // Open a KISS/TCP connection (AetherSDR default host:8001). Cancels any
    // previous connection and (re)enables auto-reconnect.
    void connectToServer(const QString& host, quint16 port);

    // Permanent disconnect — no auto-reconnect.
    void disconnectFromServer();

    bool    connected() const { return m_connected; }
    QString host()      const { return m_host; }
    quint16 port()      const { return m_port; }
    QString lastError() const { return m_lastError; }

    // TX: send an APRS text message to `addressee` via `source`. `path` is the
    // digipeater path (e.g. {"WIDE1-1"}); `msgNo` (optional) requests an ack.
    // Returns false if not connected or the frame couldn't be built.
    bool sendMessage(const QString& source, const QString& addressee,
                     const QString& text, const QStringList& path = {},
                     const QString& msgNo = QString());

    // TX: send a raw APRS info string (position, status, etc.) as a UI frame.
    bool sendInfo(const QString& source, const QString& dest,
                  const QByteArray& info, const QStringList& path = {});

signals:
    void connectionChanged(bool connected);
    // A decoded AX.25/APRS frame arrived.
    void aprsReport(const ShackBook::Aprs::Report& report);
    // Diagnostic: a decoded frame's raw AX.25 bytes (for a monitor view).
    void rawFrame(const QByteArray& ax25);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred();
    void onReconnectTimeout();

private:
    bool sendAx25(const QByteArray& ax25);
    void scheduleReconnect();
    void cancelReconnect();
    void setConnected(bool c);

    QTcpSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};

    QString m_host;
    quint16 m_port{8001};
    bool    m_userInitiatedDisconnect{false};
    bool    m_connected{false};
    int     m_reconnectAttempts{0};
    QString m_lastError;

    QByteArray m_rxBuffer;   // partial KISS bytes carried between reads
};

} // namespace ShackBook
