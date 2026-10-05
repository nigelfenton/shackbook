#include "RigctldConnectionTest.h"

#include <QTcpSocket>
#include <QTimer>

namespace ShackBook {

namespace {

// rigctld's status reply: "RPRT 0" is success, "RPRT -5" a Hamlib error code.
bool parseRprt(const QString& line, int* code)
{
    if (!line.startsWith(QLatin1String("RPRT"))) return false;
    bool ok = false;
    const int c = line.mid(4).trimmed().toInt(&ok);
    *code = ok ? c : -1;
    return true;
}

// Hamlib error codes (rig.h, enum rig_errcode_e), as rigctld reports them.
constexpr int kHamlibTimeout = -5;   // RIG_ETIMEOUT: the radio did not answer
constexpr int kHamlibIo      = -6;   // RIG_EIO: the serial port failed

// Same plausibility rule RigctldClient applies: noise from a garbled link must
// not read as "the radio answered".
bool plausibleHz(qint64 hz) { return hz > 1000 && hz < 300000000000LL; }

QString where(const RigctldTestResult& r)
{
    return QStringLiteral("%1:%2").arg(r.host).arg(r.port);
}

} // namespace

QString describeRigctldTest(const RigctldTestResult& r)
{
    using O = RigctldTestResult::Outcome;
    switch (r.outcome) {
    case O::Ok: {
        const QString radio = r.model.isEmpty() ? QStringLiteral("radio") : r.model;
        const QString freq = QString::number(r.freqMhz, 'f', 6);
        const QString mode = r.mode.isEmpty() ? QStringLiteral(" (mode not reported)")
                                              : QStringLiteral(", %1").arg(r.mode);
        return QStringLiteral("Connected to rigctld at %1: %2 reports %3 MHz%4.")
            .arg(where(r), radio, freq, mode);
    }
    case O::NotListening:
        return QStringLiteral("Nothing is listening at %1. rigctld doesn't appear to be "
                              "running there: start it with the command above, or check "
                              "the port.").arg(where(r));
    case O::HostNotFound:
        return QStringLiteral("Can't find the host \"%1\". Check the Host field.").arg(r.host);
    case O::NoAnswer:
        return QStringLiteral("No answer from %1. Check the address, and that a firewall "
                              "isn't blocking the port.").arg(where(r));
    case O::RigctldSilent:
        return QStringLiteral("Connected to %1, but nothing answered. rigctld may be stuck, "
                              "or the program on that port isn't rigctld.").arg(where(r));
    case O::RadioNotAnswering:
        return QStringLiteral("Connected to rigctld at %1, but the radio isn't answering. "
                              "Check that the baud rate matches the radio's CAT menu, the "
                              "serial port is the one the radio is on, the radio model is "
                              "right, and the radio is switched on.").arg(where(r));
    case O::SerialError:
        return QStringLiteral("rigctld at %1 can't use the serial port. Check the port, "
                              "and that no other program has it open.").arg(where(r));
    case O::NotUnderstood: {
        const QString code = r.rprt != 0 ? QStringLiteral(" (rigctld error %1)").arg(r.rprt)
                                         : QString();
        return QStringLiteral("Connected to rigctld at %1, but the radio's reply didn't make "
                              "sense%2. Check the radio model, and that the baud rate "
                              "matches.").arg(where(r), code);
    }
    case O::Dropped:
        return QStringLiteral("rigctld at %1 closed the connection during the test. It may "
                              "have stopped; check its window or output.").arg(where(r));
    }
    return {};
}

RigctldConnectionTest::RigctldConnectionTest(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &RigctldConnectionTest::onTimeout);
}

RigctldConnectionTest::~RigctldConnectionTest()
{
    // Destroyed mid-test (the Settings dialog closed): drop the socket quietly.
    // Nothing may be emitted from here.
    m_running = false;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void RigctldConnectionTest::setTimeouts(int connectMs, int replyMs)
{
    m_connectMs = connectMs;
    m_replyMs   = replyMs;
}

void RigctldConnectionTest::start(const QString& host, quint16 port)
{
    if (m_running) return;

    // A fresh socket per run, so nothing from a previous test can leak in.
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &RigctldConnectionTest::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &RigctldConnectionTest::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RigctldConnectionTest::onSocketError);
    // A remote close normally arrives as an error too; this catches it if not.
    connect(m_socket, &QTcpSocket::disconnected, this, &RigctldConnectionTest::onSocketError);

    m_result = RigctldTestResult{};
    m_result.host = host;
    m_result.port = port;
    m_rxBuf.clear();
    m_running = true;
    m_stage = Stage::Connecting;
    m_timer->start(m_connectMs);
    m_socket->connectToHost(host, port);
}

void RigctldConnectionTest::onConnected()
{
    // The frequency comes first because it is the question that proves the
    // whole chain: rigctld running, right port, right baud, right model.
    ask(Stage::Freq, QStringLiteral("f"));
}

void RigctldConnectionTest::ask(Stage next, const QString& command)
{
    m_stage = next;
    m_socket->write((command + QLatin1Char('\n')).toUtf8());
    m_timer->start(m_replyMs);
}

void RigctldConnectionTest::onReadyRead()
{
    m_rxBuf += QString::fromUtf8(m_socket->readAll());
    int nl;
    while (m_running && (nl = m_rxBuf.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_rxBuf.left(nl).trimmed();
        m_rxBuf.remove(0, nl + 1);
        if (!line.isEmpty()) handleLine(line);
    }
}

void RigctldConnectionTest::handleLine(const QString& line)
{
    using O = RigctldTestResult::Outcome;
    int code = 0;
    const bool isRprt = parseRprt(line, &code);

    switch (m_stage) {
    case Stage::Freq: {
        if (isRprt) {
            if (code == kHamlibTimeout) finish(O::RadioNotAnswering, code);
            else if (code == kHamlibIo) finish(O::SerialError, code);
            else finish(O::NotUnderstood, code);
            return;
        }
        bool ok = false;
        const qint64 hz = line.toLongLong(&ok);
        if (!ok || !plausibleHz(hz)) {
            finish(O::NotUnderstood);
            return;
        }
        m_result.freqMhz = hz / 1.0e6;
        ask(Stage::Mode, QStringLiteral("m"));
        return;
    }
    case Stage::Mode:
        // `m` answers mode then passband. A radio that cannot report mode says
        // RPRT -11 instead; the frequency has already proved the link.
        if (isRprt) {
            ask(Stage::Info, QStringLiteral("\\get_info"));
            return;
        }
        m_result.mode = line.toUpper();
        m_stage = Stage::Passband;
        return;
    case Stage::Passband:
        ask(Stage::Info, QStringLiteral("\\get_info"));
        return;
    case Stage::Info: {
        if (!isRprt) {
            QString model = line;
            if (model.startsWith(QLatin1String("Info:"), Qt::CaseInsensitive))
                model = model.mid(5).trimmed();
            // Only a printable name with a letter or digit in it, as RigctldClient.
            bool plausible = !model.isEmpty() && model.size() <= 64;
            bool hasAlnum = false;
            for (const QChar& c : model) {
                if (!c.isPrint()) { plausible = false; break; }
                if (c.isLetterOrNumber()) hasAlnum = true;
            }
            if (plausible && hasAlnum) m_result.model = model;
        }
        finish(O::Ok);
        return;
    }
    case Stage::Idle:
    case Stage::Connecting:
        return;
    }
}

void RigctldConnectionTest::onSocketError()
{
    if (!m_running) return;
    using O = RigctldTestResult::Outcome;
    const auto err = m_socket->error();

    if (m_stage == Stage::Connecting) {
        if (err == QAbstractSocket::ConnectionRefusedError) finish(O::NotListening);
        else if (err == QAbstractSocket::HostNotFoundError) finish(O::HostNotFound);
        else finish(O::NoAnswer);
        return;
    }
    // Lost after the frequency was in: the radio did answer, so report that
    // rather than throw it away because rigctld closed before \get_info.
    if (m_result.freqMhz > 0.0) {
        finish(O::Ok);
        return;
    }
    finish(O::Dropped);
}

void RigctldConnectionTest::onTimeout()
{
    if (!m_running) return;
    using O = RigctldTestResult::Outcome;
    switch (m_stage) {
    case Stage::Connecting:
        finish(O::NoAnswer);
        return;
    case Stage::Freq:
        // Not even an RPRT: rigctld always answers, so this is a wedged rigctld
        // or something else entirely on that port.
        finish(O::RigctldSilent);
        return;
    case Stage::Mode:
    case Stage::Passband:
    case Stage::Info:
        // The frequency already proved the link; a missing extra is not a failure.
        finish(O::Ok);
        return;
    case Stage::Idle:
        return;
    }
}

void RigctldConnectionTest::finish(RigctldTestResult::Outcome outcome, int rprt)
{
    if (!m_running) return;
    m_running = false;
    m_stage = Stage::Idle;
    m_timer->stop();
    m_result.outcome = outcome;
    m_result.rprt = rprt;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
    emit finished(m_result);
}

} // namespace ShackBook
