#include "RigctldSupervisor.h"

#include <QFileInfo>
#include <QTcpSocket>

namespace ShackBook {

RigctldSupervisor::RigctldSupervisor(QObject* parent) : QObject(parent) {}

RigctldSupervisor::~RigctldSupervisor()
{
    // The leak this class exists to prevent. Without it, a rigctld we spawned
    // outlives ShackBook holding the serial port, with no window to close.
    stopIfOurs();
}

bool RigctldSupervisor::serverAnswering(const QString& host, quint16 port,
                                        int timeoutMs)
{
    // Deliberately only a TCP connect, not a rigctl handshake. The question
    // being asked is "would spawning a second server collide with something",
    // and anything holding the port collides -- healthy rigctld or not.
    QTcpSocket probe;
    probe.connectToHost(host, port);
    return probe.waitForConnected(timeoutMs);
}

QStringList RigctldSupervisor::buildArguments(const QString& model,
                                              const QString& serialPort,
                                              const QString& baud)
{
    QStringList args{QStringLiteral("-m"), model,
                     QStringLiteral("-r"), serialPort};
    // Baud is optional: Hamlib has a per-model default, and forcing an empty
    // -s would be worse than omitting it.
    if (!baud.isEmpty())
        args << QStringLiteral("-s") << baud;
    return args;
}

void RigctldSupervisor::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

RigctldSupervisor::State RigctldSupervisor::ensureRunning(
    const QString& rigctldPath, const QString& host, quint16 port,
    const QString& model, const QString& serialPort, const QString& baud)
{
    m_lastError.clear();

    // ADOPT FIRST, ALWAYS. Someone else's rigctld may be serving WSJT-X from
    // the same port; spawning a second would fight it for the serial device.
    // Checking before spawning is what makes auto-start safe rather than
    // merely convenient.
    if (serverAnswering(host, port)) {
        setState(State::Adopted);
        return m_state;
    }

    if (rigctldPath.isEmpty() || !QFileInfo::exists(rigctldPath)) {
        m_lastError = tr("rigctld was not found.");
        setState(State::Failed);
        return m_state;
    }
    if (model.isEmpty() || serialPort.isEmpty()) {
        m_lastError = tr("A radio model and serial port are needed before "
                         "rigctld can be started.");
        setState(State::Failed);
        return m_state;
    }

    if (!m_proc) {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus) {
                    // Only meaningful while we believe we own one. stopIfOurs()
                    // sets Stopped before waiting, so a deliberate stop does
                    // not masquerade as a crash.
                    if (m_state != State::Ours)
                        return;
                    const QString out =
                        QString::fromUtf8(m_proc->readAll()).trimmed();
                    setState(State::Stopped);
                    emit exitedUnexpectedly(
                        out.isEmpty()
                            ? tr("rigctld exited (code %1).").arg(code)
                            : out);
                });
    }

    m_proc->start(rigctldPath, buildArguments(model, serialPort, baud));
    if (!m_proc->waitForStarted(3000)) {
        m_lastError = tr("rigctld would not start: %1").arg(m_proc->errorString());
        setState(State::Failed);
        return m_state;
    }

    // STARTED IS NOT SERVING. rigctld exits almost immediately on a bad port
    // or a busy device, and treating "the process launched" as success is how
    // an operator ends up staring at a dead link that reported fine. Wait for
    // the port to actually answer.
    for (int waited = 0; waited < 3000; waited += 200) {
        if (m_proc->state() != QProcess::Running) {
            // Died during startup -- its own output says why, and "port busy"
            // and "no such device" need different fixes.
            const QString out = QString::fromUtf8(m_proc->readAll()).trimmed();
            m_lastError = out.isEmpty() ? tr("rigctld exited during startup.") : out;
            setState(State::Failed);
            return m_state;
        }
        if (serverAnswering(host, port, 200)) {
            setState(State::Ours);
            return m_state;
        }
    }

    m_lastError = tr("rigctld started but is not answering on %1:%2.")
                      .arg(host).arg(port);
    // Do not leave a half-working process behind: it holds the serial port
    // while serving nobody, which is the worst of both.
    m_proc->kill();
    m_proc->waitForFinished(1000);
    setState(State::Failed);
    return m_state;
}

void RigctldSupervisor::stopIfOurs()
{
    // An adopted server belongs to someone else and must outlive us. This
    // check is the difference between supervising and trampling.
    if (m_state != State::Ours || !m_proc)
        return;
    // Set state BEFORE stopping so the finished handler does not report our
    // own deliberate shutdown as an unexpected exit.
    setState(State::Stopped);
    m_proc->terminate();
    if (!m_proc->waitForFinished(2000))
        m_proc->kill();
    m_proc->waitForFinished(1000);
}

}  // namespace ShackBook
