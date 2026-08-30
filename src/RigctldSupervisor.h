#pragma once

// Starts rigctld when nothing is already serving, and stops ONLY what it
// started.
//
// ShackBook has always refused to launch rigctld, on the grounds that grabbing
// a serial port could take it from WSJT-X or fldigi. That reasoning does not
// survive use: the operator ends up running rigctld anyway, so the port is
// taken either way -- the only difference is that they must keep a terminal
// open to do it. See issue #13.
//
// The real hazard is not ownership, it is LIFECYCLE. A rigctld this process
// spawned and then leaked -- because ShackBook crashed or was killed -- holds
// the port with no window to close, which is worse than a terminal you can
// see. So the rules here are deliberately narrow:
//
//   * ADOPT, NEVER SPAWN, if something already answers on the port. Yours may
//     be shared with another program; a second instance would fight it.
//   * STOP ONLY WHAT WE STARTED. An adopted server belongs to somebody else
//     and must outlive us.
//   * NEVER LEAVE ONE RUNNING that we started -- stop it on shutdown.
//   * SAY WHICH IT IS. "Connected" hides the difference between a server we
//     own and one we borrowed, and only one of those is ours to restart.

#include <QObject>
#include <QProcess>
#include <QString>

namespace ShackBook {

class RigctldSupervisor : public QObject {
    Q_OBJECT

public:
    explicit RigctldSupervisor(QObject* parent = nullptr);
    ~RigctldSupervisor() override;

    // How the server on the port came to be there.
    enum class State {
        Stopped,   // nothing running, nothing adopted
        Adopted,   // someone else's server is answering; not ours to stop
        Ours,      // we spawned it, and we must stop it
        Failed,    // we tried to spawn and it did not come up
    };

    // Is something already answering on host:port? A plain TCP connect: this
    // asks whether the PORT is served, not whether rigctld is healthy, which
    // is the question that decides adopt-vs-spawn.
    [[nodiscard]] static bool serverAnswering(const QString& host, quint16 port,
                                              int timeoutMs = 600);

    // The command line, exactly as it would be run. Exposed so the settings
    // dialog can SHOW it rather than describing it, and so a test can assert
    // the arguments without launching anything.
    [[nodiscard]] static QStringList buildArguments(const QString& model,
                                                    const QString& port,
                                                    const QString& baud);

    // Adopt if something answers; otherwise spawn. Returns the resulting
    // state. `rigctldPath` is the executable; empty means "not found", which
    // can only ever produce Failed.
    State ensureRunning(const QString& rigctldPath, const QString& host,
                        quint16 port, const QString& model,
                        const QString& serialPort, const QString& baud);

    // Stop the server IF we started it. A no-op for an adopted one -- that is
    // the whole point.
    void stopIfOurs();

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool ours() const { return m_state == State::Ours; }
    // Whatever rigctld printed when a spawn failed. The useful half of a
    // failure: "port busy" and "no such device" need different fixes.
    [[nodiscard]] QString lastError() const { return m_lastError; }

signals:
    void stateChanged(State state);
    // rigctld exited on its own. Distinct from us stopping it: an operator
    // whose radio link silently died needs telling, and the old design could
    // not tell them because it never knew the process existed.
    void exitedUnexpectedly(const QString& detail);

private:
    QProcess* m_proc{nullptr};
    State     m_state{State::Stopped};
    QString   m_lastError;

    void setState(State s);
};

}  // namespace ShackBook
