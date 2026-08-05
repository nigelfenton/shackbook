#include "TciDiscovery.h"

#include "TciClient.h"

#include <QSharedPointer>
#include <QTimer>

namespace ShackLog {

TciDiscovery::TciDiscovery(QObject* parent) : QObject(parent) {}

TciDiscovery::~TciDiscovery() = default;

QList<quint16> TciDiscovery::defaultPorts()
{
    // Deliberately short. A wide sweep is slow, looks like a port scan to
    // anything watching the network, and buys little: TCI servers in practice
    // sit on 40001, and a second instance lands on the next port up.
    return {40001, 40002, 50001, 50002};
}

void TciDiscovery::scan(const QStringList& hosts, const QList<quint16>& ports,
                        int perProbeTimeoutMs)
{
    if (running()) return;

    m_found.clear();
    m_cancelled = false;
    m_done = 0;

    QStringList uniqueHosts;
    for (const QString& h : hosts) {
        const QString t = h.trimmed();
        if (!t.isEmpty() && !uniqueHosts.contains(t)) uniqueHosts << t;
    }
    if (uniqueHosts.isEmpty()) uniqueHosts << QStringLiteral("127.0.0.1");

    m_total = uniqueHosts.size() * ports.size();
    m_outstanding = m_total;
    if (m_total == 0) {
        emit finished(m_found);
        return;
    }

    for (const QString& host : uniqueHosts)
        for (quint16 port : ports)
            probeOne(host, port, perProbeTimeoutMs);
}

void TciDiscovery::probeOne(const QString& host, quint16 port, int timeoutMs)
{
    auto* client = new TciClient(this);
    client->setProbeMode(true);   // one shot; never retry a dead port
    auto* timer  = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(timeoutMs);

    // Shared between the lambdas so the probe resolves exactly once, whether
    // it succeeds, fails, or times out.
    auto settled = QSharedPointer<bool>::create(false);

    auto finish = [this, client, timer, settled](bool ok, const TciServerInfo& info) {
        if (*settled) return;
        *settled = true;
        timer->stop();
        client->disconnectFromServer();
        client->deleteLater();
        timer->deleteLater();
        if (ok && !m_cancelled) {
            m_found.append(info);
            emit serverFound(info);
        }
        oneDone();
    };

    // A server counts as TCI only when it sends something that PARSES as TCI.
    // `device:` is the ideal answer; `protocol:` alone is accepted because
    // some servers announce it first and are slower with the rest.
    connect(client, &TciClient::deviceNameChanged, this,
            [client, host, port, finish](const QString& device) {
                if (device.isEmpty()) return;   // the clear-on-disconnect case
                TciServerInfo info;
                info.host = host;
                info.port = port;
                info.device = device;
                info.protocolName = client->serverProtocolName();
                info.protocolVer  = client->serverProtocolVersion();
                finish(true, info);
            });

    connect(client, &TciClient::serverInfoChanged, this,
            [client, host, port, timer](const QString& name, const QString& ver) {
                Q_UNUSED(name); Q_UNUSED(ver);
                // Protocol seen: this is very likely TCI, but give `device:`
                // a moment to arrive so the result carries a useful name.
                if (timer->isActive() && timer->remainingTime() < 400)
                    timer->start(400);
            });

    // Timeout is the normal outcome for a port with nothing on it, and also
    // for a port with something non-TCI on it. Both are "not found".
    connect(timer, &QTimer::timeout, this, [client, host, port, finish]() {
        const QString proto = client->serverProtocolName();
        if (!proto.isEmpty()) {
            // Answered `protocol:` but never `device:` — still a TCI server,
            // just a quiet one. Report it with no device name.
            TciServerInfo info;
            info.host = host;
            info.port = port;
            info.protocolName = proto;
            info.protocolVer  = client->serverProtocolVersion();
            finish(true, info);
            return;
        }
        finish(false, {});
    });

    timer->start();
    client->connectToServer(host, port);
}

void TciDiscovery::oneDone()
{
    ++m_done;
    emit progress(m_done, m_total);
    if (--m_outstanding <= 0) {
        m_outstanding = 0;
        emit finished(m_found);
    }
}

void TciDiscovery::cancel()
{
    m_cancelled = true;
}

} // namespace ShackLog
