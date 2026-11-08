#pragma once

// RigctldConnectionTest — "is this rigctld actually talking to my radio?" (#15)
//
// The Settings "Test connection" button. It answers the question the rest of
// the app cannot: not "did a socket open" but "did a radio answer". A socket
// that accepts a connection is not a radio, which is the same reasoning the
// TCI scan already applies ("only servers that answer a real handshake").
//
// ⭐ The case this exists for is the one that LOOKS like success today: a wrong
// baud rate or radio model. rigctld starts, ShackBook connects to it, and the
// radio simply never answers. Hamlib reports that as `RPRT -5` (timeout) after
// its own retries, so the test waits long enough to hear it and then names the
// likely cause instead of the symptom.
//
// ⛔ READ-ONLY, like RigctldClient: it sends `f`, `m` and `\get_info` and nothing
// else. No PTT, no set-frequency. tests/rigctld_connection_test.cpp pins the
// exact command list.
//
// One-shot: it never reconnects or retries. Run it again to test again.

#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace ShackBook {

struct RigctldTestResult {
    enum class Outcome {
        Ok,                 // the radio reported a frequency
        NotListening,       // connection refused: rigctld is not running there
        HostNotFound,       // the host name does not resolve
        NoAnswer,           // the connection attempt timed out (wrong address, firewall)
        RigctldSilent,      // connected, but nothing ever replied
        RadioNotAnswering,  // rigctld timed out talking to the radio (RPRT -5)
        SerialError,        // rigctld cannot use the serial port (RPRT -6)
        NotUnderstood,      // a reply that is not a frequency, or another RPRT error
        Dropped,            // rigctld closed the connection mid-test
    };

    Outcome outcome{Outcome::NoAnswer};
    QString host;
    quint16 port{0};
    double  freqMhz{0.0};
    QString mode;       // empty when the radio does not report one
    QString model;      // from \get_info; empty when not offered
    int     rprt{0};    // rigctld's error code, when the outcome came from one
};

// The operator-facing sentence for a result: what was found and what to check.
// Pure, so the wording is tested without a network.
QString describeRigctldTest(const RigctldTestResult& r);

class RigctldConnectionTest : public QObject {
    Q_OBJECT

public:
    explicit RigctldConnectionTest(QObject* parent = nullptr);
    ~RigctldConnectionTest() override;

    // How long to wait for the TCP connection, and for each reply. The reply
    // wait is long by default on purpose: Hamlib only reports an unanswering
    // radio after its own timeout and retries (several seconds on an Icom), and
    // giving up first would turn the most useful diagnosis into "rigctld silent".
    void setTimeouts(int connectMs, int replyMs);

    void start(const QString& host, quint16 port);
    bool running() const { return m_running; }

signals:
    void finished(const ShackBook::RigctldTestResult& result);

private:
    enum class Stage { Idle, Connecting, Freq, Mode, Passband, Info };

    void onConnected();
    void onReadyRead();
    void onSocketError();
    void onTimeout();
    void handleLine(const QString& line);
    void ask(Stage next, const QString& command);
    void finish(RigctldTestResult::Outcome outcome, int rprt = 0);

    QTcpSocket* m_socket{nullptr};
    QTimer*     m_timer{nullptr};
    Stage       m_stage{Stage::Idle};
    bool        m_running{false};
    // Generous because Windows retries a refused connection for about 2 s before
    // reporting it; a shorter wait would call a stopped rigctld "no answer".
    int         m_connectMs{5000};
    int         m_replyMs{12000};
    QString     m_rxBuf;
    RigctldTestResult m_result;
};

} // namespace ShackBook
