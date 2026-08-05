#include "RigctldClient.h"

#include <QTcpSocket>
#include <QTimer>

namespace ShackLog {

namespace {

// Same backoff ladder TciClient uses, for the same reason: a radio that is off
// should not be hammered, but a cable pushed back in should recover quickly.
int backoffSeconds(int attempt)
{
    static const int ladder[] = {1, 2, 5, 10, 30};
    const int n = int(sizeof(ladder) / sizeof(ladder[0]));
    return ladder[attempt < n ? attempt : n - 1];
}

} // namespace

RigctldClient::RigctldClient(QObject* parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)),
      m_pollTimer(new QTimer(this)),
      m_reconnectTimer(new QTimer(this)),
      m_replyTimer(new QTimer(this))
{
    m_replyTimer->setSingleShot(true);
    m_replyTimer->setInterval(2000);
    connect(m_replyTimer, &QTimer::timeout, this, &RigctldClient::onReplyTimeout);

    connect(m_socket, &QTcpSocket::connected,    this, &RigctldClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RigctldClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &RigctldClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RigctldClient::onErrorOccurred);

    m_pollTimer->setInterval(m_pollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &RigctldClient::onPollTimeout);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &RigctldClient::onReconnectTimeout);
}

RigctldClient::~RigctldClient() = default;

void RigctldClient::setPollIntervalMs(int ms)
{
    m_pollIntervalMs = ms > 50 ? ms : 50;   // a floor; below this is pointless
    m_pollTimer->setInterval(m_pollIntervalMs);
}

void RigctldClient::connectToServer(const QString& host, quint16 port)
{
    m_userInitiatedDisconnect = false;
    m_reconnectAttempts = 0;
    m_host = host;
    m_port = port;
    m_rxBuf.clear();
    m_expect = Expect::None;

    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    m_socket->connectToHost(host, port);
}

void RigctldClient::disconnectFromServer()
{
    m_userInitiatedDisconnect = true;
    m_reconnectTimer->stop();
    m_pollTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
    setConnected(false);
}

void RigctldClient::onConnected()
{
    m_reconnectAttempts = 0;
    setConnected(true);

    // Ask what this radio is before polling starts. The answer is the model
    // name that will be stamped on every QSO, so it is worth having early.
    m_expect = Expect::Info;
    send(QStringLiteral("\\get_info"));

    m_pollTimer->start();
}

void RigctldClient::onDisconnected()
{
    m_pollTimer->stop();

    // Forget the model. A QSO logged after the radio goes away must not be
    // stamped with the PREVIOUS radio's name — a wrong attribution is worse
    // than a blank one. Same rule already proven for TCI.
    if (!m_model.isEmpty()) {
        m_model.clear();
        emit modelNameChanged(m_model);
    }
    m_expect = Expect::None;
    m_rxBuf.clear();

    setConnected(false);
    if (!m_userInitiatedDisconnect) scheduleReconnect();
}

void RigctldClient::onErrorOccurred()
{
    m_lastError = m_socket->errorString();
}

void RigctldClient::onPollTimeout()
{
    if (!m_connected) return;

    // ⚠ One question at a time. rigctld answers with a bare value and no echo
    // of the command, so the only way to know what a reply means is to
    // remember what was asked. Pipelining two requests makes the replies
    // ambiguous and silently mis-assigns mode to frequency.
    if (m_expect != Expect::None) return;   // still waiting; skip this tick

    m_expect = Expect::Freq;
    send(QStringLiteral("f"));
}

void RigctldClient::send(const QString& line)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_socket->write((line + QLatin1Char('\n')).toUtf8());
    // Arm the watchdog whenever a reply is owed, so a radio that goes quiet
    // cannot wedge the state machine.
    if (m_expect != Expect::None) m_replyTimer->start();
}

void RigctldClient::onReplyTimeout()
{
    // No answer in time. Clear the expectation so the next poll can proceed —
    // a stalled radio should recover on its own once it starts answering
    // again, not require a reconnect.
    m_expect = Expect::None;
    m_rxBuf.clear();
}

void RigctldClient::onReadyRead()
{
    m_rxBuf += QString::fromUtf8(m_socket->readAll());

    int nl;
    while ((nl = m_rxBuf.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_rxBuf.left(nl);
        m_rxBuf.remove(0, nl + 1);
        line.remove(QLatin1Char('\r'));
        handleLine(line.trimmed());
    }

    // A reply that never arrives would wedge the state machine forever, and a
    // wedged poll looks exactly like a radio that stopped moving. Cap the
    // buffer so garbage cannot grow without bound either.
    if (m_rxBuf.size() > 8192) m_rxBuf.clear();
}

void RigctldClient::handleLine(const QString& line)
{
    if (line.isEmpty()) return;
    emit rawLineReceived(line);
    m_replyTimer->stop();   // something answered; the watchdog can stand down

    // `RPRT n` is rigctld's status reply. RPRT 0 is success with no value;
    // anything else is an error — including RPRT -11 for "not implemented",
    // which is what a radio that cannot report mode will say.
    if (line.startsWith(QLatin1String("RPRT"))) {
        m_expect = Expect::None;
        return;
    }

    switch (m_expect) {
    case Expect::Info: {
        // "Info: IC-9700" — the model name. Some versions answer bare.
        QString model = line;
        if (model.startsWith(QLatin1String("Info:"), Qt::CaseInsensitive))
            model = model.mid(5).trimmed();

        // ⛔ A model name is stamped on every QSO, so it must not be whatever
        // bytes happened to arrive. A garbled link (RF desense at Field Day)
        // returns control characters and punctuation; storing that as the
        // radio would put junk in the log permanently. Require something that
        // looks like a radio name: printable, and containing a letter or digit.
        bool plausible = !model.isEmpty() && model.size() <= 64;
        if (plausible) {
            bool hasAlnum = false;
            for (const QChar& c : model) {
                if (!c.isPrint()) { plausible = false; break; }
                if (c.isLetterOrNumber()) hasAlnum = true;
            }
            plausible = plausible && hasAlnum;
        }

        if (plausible && model != m_model) {
            m_model = model;
            emit modelNameChanged(m_model);
        }
        m_expect = Expect::None;
        break;
    }
    case Expect::Freq: {
        bool ok = false;
        const qint64 hz = line.toLongLong(&ok);
        // ⛔ Only believe a plausible number. A garbled serial link — the
        // classic RF-desense failure at Field Day — returns noise, and storing
        // that as a frequency puts a wrong number on a real QSO. 1 kHz to
        // 300 GHz spans anything Hamlib can legitimately report.
        if (ok && hz > 1000 && hz < 300000000000LL) {
            const double mhz = hz / 1.0e6;
            if (qAbs(mhz - m_freqMhz) > 1.0e-9) {
                m_freqMhz = mhz;
                emit frequencyChanged(m_freqMhz);
            }
        }
        // Frequency answered; ask for mode next. Chaining here rather than in
        // the timer keeps exactly one request outstanding at all times.
        m_expect = Expect::Mode;
        send(QStringLiteral("m"));
        break;
    }
    case Expect::Mode: {
        // `m` answers on TWO lines: mode, then passband. Take the mode and
        // wait for the passband so it is not mistaken for the next reply.
        const QString m = line.trimmed().toUpper();
        if (!m.isEmpty() && m != m_mode) {
            m_mode = m;
            emit modeChanged(m_mode);
        }
        m_expect = Expect::ModePassband;
        break;
    }
    case Expect::ModePassband:
        // Passband in Hz. Not used by the logbook, but it must be consumed or
        // it would be read as the answer to the next question.
        m_expect = Expect::None;
        break;

    case Expect::None:
        // Unsolicited traffic. Real rigctld does not push, so this is either a
        // late reply or a server that is not rigctld at all. Ignore it rather
        // than guess what it meant.
        break;
    }
}

void RigctldClient::scheduleReconnect()
{
    if (m_userInitiatedDisconnect) return;
    if (m_probeMode) return;   // discovery probes are one-shot by design
    const int secs = backoffSeconds(m_reconnectAttempts);
    ++m_reconnectAttempts;
    m_reconnectTimer->start(secs * 1000);
}

void RigctldClient::onReconnectTimeout()
{
    if (m_userInitiatedDisconnect || m_probeMode) return;
    if (m_host.isEmpty()) return;
    m_socket->abort();
    m_socket->connectToHost(m_host, m_port);
}

void RigctldClient::setConnected(bool c)
{
    if (c == m_connected) return;
    m_connected = c;
    emit connectionChanged(m_connected);
}

} // namespace ShackLog
