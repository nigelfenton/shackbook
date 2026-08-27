#pragma once

// RigctldClient — follow a radio through Hamlib's rigctld.
//
// ShackBook speaks TCI, which covers AetherSDR, ExpertSDR and SunSDR and nothing
// else. Every other radio — Icom, Yaesu, Kenwood — is reached through Hamlib,
// which drives ~200 of them and exposes a short line-oriented TCP protocol on
// :4532. One client class here buys all of them, which is why this exists
// instead of per-radio CAT drivers.
//
// ⚠ ShackBook does NOT ship Hamlib (decided 2026-08-05). `rigctld` must already
// be installed and running; when it is missing the operator has to be told
// exactly that, because a missing rigctld, a radio switched off, a wrong port
// and a firewall all present identically as "no connection".
//
// Deliberately mirrors TciClient's shape — same signals, same connect/
// disconnect/probe surface — so MainWindow can treat the two interchangeably.
//
// ⛔ READ-ONLY BY CONSTRUCTION. This sends only `f`, `m`, `\get_info` and
// `\dump_state`. There is no PTT and no set-frequency, and there must never be:
// a logbook has no business keying a radio, and RULE 1 applies to anything that
// could put a signal on the air.
//
// Polling, not streaming: rigctld has no event push, so frequency and mode are
// asked for on a timer and signals fire only on change — same contract
// TciClient offers, so the rest of the app cannot tell the difference.

#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace ShackBook {

// Settings key for the operator-set nickname of the radio at this endpoint.
// Separate from the TCI one so a rigctld radio and a TCI server on the same
// host:port cannot collide.
inline QString rigctldNicknameKey(const QString& host, const QString& port)
{
    return QStringLiteral("RIGCTLD_NICKNAME_%1_%2").arg(host, port);
}

class RigctldClient : public QObject {
    Q_OBJECT

public:
    explicit RigctldClient(QObject* parent = nullptr);
    ~RigctldClient() override;

    // Hamlib's default port, and the next one up for a second instance.
    static quint16 defaultPort() { return 4532; }

    // ── Is Hamlib actually here? ───────────────────────────────────────
    // ShackBook does not ship Hamlib, so the operator may simply not have it.
    // ⛔ That must never look like a connection failure: "not installed", "a
    // radio that is off", "the wrong port" and "a firewall" all present as
    // no-connection, and only the first has a different fix.
    //
    // Resolution order mirrors LotwDialog's tqsl lookup, which is proven in
    // this codebase: a configured path wins, then the usual install locations,
    // then PATH. Returns an empty string when nothing was found.
    static QString findRigctld(const QString& configuredPath = {});

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();

    // One-shot mode for discovery probes — never auto-reconnect. Same reason
    // as TciClient::setProbeMode: without it a probe against a dead port
    // retries forever and a scan leaves a timer per port running.
    void setProbeMode(bool probe) { m_probeMode = probe; }

    // How often to ask the radio where it is. 500 ms is responsive enough to
    // feel live while leaving a busy serial link alone; rigctld is a poll-only
    // protocol, so this is a real cost on the radio's CAT port.
    void setPollIntervalMs(int ms);

    bool    connected()           const { return m_connected; }
    double  currentFrequencyMhz() const { return m_freqMhz; }
    QString currentMode()         const { return m_mode; }
    QString lastError()           const { return m_lastError; }

    // Model name from `\get_info` (e.g. "IC-9700"). ⭐ Unlike TCI's `device:`,
    // which names the APPLICATION, this is the RADIO — so it is a genuinely
    // better answer for "which rig made this QSO" and needs no nickname to
    // disambiguate two rigs behind one program.
    QString modelName()           const { return m_model; }

signals:
    void connectionChanged(bool connected);
    void frequencyChanged(double mhz);
    void modeChanged(const QString& mode);
    void modelNameChanged(const QString& model);
    // Diagnostic — every line received, for a protocol inspector.
    void rawLineReceived(const QString& line);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred();
    void onPollTimeout();
    void onReconnectTimeout();
    void onReplyTimeout();

private:
    void send(const QString& line);
    void handleLine(const QString& line);
    void scheduleReconnect();
    void setConnected(bool c);

    // What the next reply is an answer to. rigctld replies are bare values with
    // no echo of the command, so the only way to read them is to remember what
    // was asked — and therefore to ask one thing at a time.
    enum class Expect { None, Freq, Mode, ModePassband, Info };

    QTcpSocket* m_socket{nullptr};
    QTimer*     m_pollTimer{nullptr};
    QTimer*     m_reconnectTimer{nullptr};
    // Guards the one-request-at-a-time state machine. A radio that accepts a
    // command and never answers (the `silent` fault, and a real symptom of a
    // wedged CAT link) would otherwise leave m_expect set forever and stop all
    // polling — silently, looking exactly like a radio that stopped moving.
    QTimer*     m_replyTimer{nullptr};

    QString m_host;
    quint16 m_port{0};
    QString m_rxBuf;

    Expect  m_expect{Expect::None};
    bool    m_userInitiatedDisconnect{false};
    bool    m_probeMode{false};
    bool    m_connected{false};
    int     m_reconnectAttempts{0};
    int     m_pollIntervalMs{500};

    double  m_freqMhz{0.0};
    QString m_mode;
    QString m_model;
    QString m_lastError;
};

} // namespace ShackBook
