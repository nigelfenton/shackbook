#pragma once

// TciDiscovery — find TCI servers on the network without being told where.
//
// ⚠ An open port is NOT a TCI server. Plenty of things listen on a port and
// will happily accept a WebSocket upgrade, and 40001/50001 in particular are
// squatted by other tools in this shack (a replay server once bound 40001 and
// silently stole the radio's clients). So discovery does a REAL handshake:
// connect, send `start;`, and only report a server that answers with traffic
// that actually parses as TCI. Anything else is discarded, however inviting
// its port looked.
//
// Each candidate is probed with its own short-lived TciClient, so the app's
// live connection is never disturbed by a scan.

#include <QList>
#include <QObject>
#include <QString>

namespace ShackBook {

class TciClient;

// One server that answered a real TCI handshake.
struct TciServerInfo {
    QString host;
    quint16 port{0};
    QString device;        // from `device:` — the APPLICATION, not the radio
    QString protocolName;  // from `protocol:`
    QString protocolVer;

    // "AetherSDR at 127.0.0.1:40001", or host:port when nothing was announced.
    QString label() const
    {
        const QString where = QStringLiteral("%1:%2").arg(host).arg(port);
        return device.isEmpty() ? where
                                : QStringLiteral("%1 at %2").arg(device, where);
    }
};

class TciDiscovery : public QObject {
    Q_OBJECT
public:
    explicit TciDiscovery(QObject* parent = nullptr);
    ~TciDiscovery() override;

    // Ports worth trying, in the order they are most likely to pay off.
    // 40001 is AetherSDR/ExpertSDR3; 50001 appears as a default in some
    // builds and documentation; the rest cover a second instance on the
    // same machine (AetherSDR's multi-instance work uses adjacent ports).
    static QList<quint16> defaultPorts();

    // Hosts to sweep: always loopback, plus anything the caller adds (the
    // currently-configured host, and previously-seen servers).
    void scan(const QStringList& hosts, const QList<quint16>& ports,
              int perProbeTimeoutMs = 1500);

    bool running() const { return m_outstanding > 0; }
    void cancel();

signals:
    // Emitted as each server answers, so a dialog can fill in progressively.
    void serverFound(const ShackBook::TciServerInfo& info);
    void progress(int done, int total);
    void finished(const QList<ShackBook::TciServerInfo>& servers);

private:
    void probeOne(const QString& host, quint16 port, int timeoutMs);
    void oneDone();

    QList<TciServerInfo> m_found;
    int  m_outstanding{0};
    int  m_total{0};
    int  m_done{0};
    bool m_cancelled{false};
};

} // namespace ShackBook

Q_DECLARE_METATYPE(ShackBook::TciServerInfo)
