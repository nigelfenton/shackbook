#pragma once

// TciClient — WebSocket client speaking the EESDR TCI protocol.
//
// TCI (Transceiver Control Interface) is the wire protocol used by
// AetherSDR, ExpertSDR2, SunSDR / Expert Electronics radios.  Messages are
// short ASCII text, each terminated with `;`, of the form
//   command:arg1,arg2,...;
// We connect, send `start;` to request event streaming, then watch for
// `vfo:` and `mode:` events on RX 0 / VFO 0 (the primary receiver).
//
// Frequency on the wire is in Hz; we normalise to MHz for the rest of the
// app since that's how operators talk about freqs and how ADIF stores them.
//
// Auto-reconnect: if the socket closes for any reason, we retry with an
// exponential backoff (1, 2, 5, 10, 30 s) until the user explicitly calls
// disconnectFromServer().

#include <QObject>
#include <QString>
#include <QUrl>

class QWebSocket;
class QTimer;

namespace ShackBook {

// Settings key holding the operator-set nickname for the radio reached at
// this endpoint. Per host:port so two radios keep separate names; shared here
// because both SettingsDialog (writes) and MainWindow (reads) need the same
// key, and a mismatch would silently lose the nickname.
inline QString tciNicknameKey(const QString& host, const QString& port)
{
    return QStringLiteral("TCI_NICKNAME_%1_%2").arg(host, port);
}

// ADIF mode -> TCI modulation string, as a pure function so the mapping can
// be tested without a radio, a socket, or a TciClient.
//
// `currentMhz` exists only to resolve "SSB", which is not a sideband: below
// 10 MHz it means LSB and above it USB, the same rule an operator applies
// without thinking. Getting that wrong puts the radio on the opposite
// sideband and the DX sounds like nothing at all.
//
// Returns an empty string when the mode is unknown, ambiguous, or when SSB
// cannot be resolved because no frequency is known — every one of which
// means "leave the radio alone" rather than "pick something".
QString tciModulationForAdifMode(const QString& adifMode, double currentMhz);


class TciClient : public QObject {
    Q_OBJECT

public:
    explicit TciClient(QObject* parent = nullptr);
    ~TciClient() override;

    // Open ws://host:port and request event streaming.  Cancels any
    // previous connection.  host is an IP or hostname; port is typically
    // 40001 for AetherSDR, 50001 for ExpertSDR2.
    void connectToServer(const QString& host, quint16 port);

    // Permanent disconnect — does not auto-reconnect.
    void disconnectFromServer();

    // One-shot mode for discovery probes: never auto-reconnect, whatever
    // happens. Without this a probe against a dead port retries on a backoff
    // forever, and a scan of a dozen ports leaves a dozen retry timers running
    // behind the app. Set it BEFORE connectToServer().
    void setProbeMode(bool probe) { m_probeMode = probe; }

    bool    connected()             const { return m_connected; }
    double  currentFrequencyMhz()   const { return m_freqMhz; }
    QString currentMode()           const { return m_mode; }
    QString serverProtocolName()    const { return m_protoName; }
    QString serverProtocolVersion() const { return m_protoVersion; }
    QString lastError()             const { return m_lastError; }
    QUrl    currentUrl()            const { return m_url; }

    // The name the server announces for itself via `device:`.
    //
    // ⚠ This is the APPLICATION, not the radio: AetherSDR answers
    // "AetherSDR" whether a Hermes-Lite 2 or a FLEX-6700 is behind it. Two
    // radios driven by the same application are indistinguishable here, which
    // is why a user-set nickname overrides this when attributing a QSO.
    QString deviceName()            const { return m_device; }

    // ── Tuning ────────────────────────────────────────────────────────
    //
    // Until now this client was read-only: it followed the radio so a QSO
    // could be logged with the right frequency and mode. These SEND, which
    // means a bug here moves somebody's radio mid-QSO rather than merely
    // showing a wrong number. Both are deliberately narrow — RX0/VFO0, no
    // split handling, no TX — and both no-op when not connected rather than
    // queueing, because a tune that lands after the operator has moved on is
    // worse than one that never happened.
    //
    // Return false when nothing was sent, so the caller can say so rather
    // than leaving the operator wondering whether the click registered.
    bool tuneToMhz(double mhz);

    // Best-effort: an unrecognised mode is not sent and returns false,
    // leaving the radio on whatever it had. Spot sources often omit the mode
    // entirely, so failing here must not prevent the frequency change.
    bool setModeString(const QString& adifMode);


signals:
    void connectionChanged(bool connected);
    void frequencyChanged(double mhz);
    void modeChanged(const QString& mode);
    void serverInfoChanged(const QString& name, const QString& version);
    void deviceNameChanged(const QString& device);
    // Diagnostic — every line received, after stripping the trailing ';'.
    void rawMessageReceived(const QString& line);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& message);
    void onErrorOccurred();
    void onReconnectTimeout();

private:
    void send(const QString& cmd);
    void parseLine(const QString& line);
    void scheduleReconnect();
    void cancelReconnect();
    void setConnected(bool c);

    QWebSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};

    QUrl    m_url;
    bool    m_userInitiatedDisconnect{false};
    bool    m_probeMode{false};
    bool    m_connected{false};
    int     m_reconnectAttempts{0};

    double  m_freqMhz{0.0};
    QString m_mode;
    QString m_protoName;
    QString m_protoVersion;
    QString m_device;
    QString m_lastError;
};

} // namespace ShackBook
