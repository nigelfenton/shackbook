#include "WsprClient.h"

#include <QDateTime>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>

#include <cmath>

namespace ShackBook {

namespace {

// A WSPR transmission occupies a 2-minute cycle, so a spot stays interesting
// for a while — a station heard once is likely still beaconing. 20 minutes
// (10 cycles) keeps the panel useful without showing paths that have closed.
constexpr int kWsprSpotLifetimeSec = 1200;

// Floor on the poll interval. The agent runs on the radio's single ARM core
// alongside wsprd; hammering it steals cycles from the decode this feed
// depends on. New spots can only appear once per 120 s cycle regardless.
constexpr int kMinPollSec = 10;

} // namespace

WsprClient::WsprClient(QObject* parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this)),
      m_pollTimer(new QTimer(this))
{
    connect(m_pollTimer, &QTimer::timeout, this, &WsprClient::poll);
}

WsprClient::~WsprClient() = default;

bool WsprClient::active() const
{
    return m_pollTimer && m_pollTimer->isActive();
}

void WsprClient::setAgent(const QString& host, int port)
{
    const QString h = host.trimmed();
    if (h.isEmpty() || port <= 0 || port > 65535) {
        m_url = QUrl{};
        return;
    }
    QUrl u;
    u.setScheme(QStringLiteral("http"));
    u.setHost(h);
    u.setPort(port);
    u.setPath(QStringLiteral("/v1/status"));
    if (u != m_url) {
        m_url = u;
        // A different agent means a different spot history; don't suppress its
        // spots because of timestamps seen from the previous one.
        m_lastSpotT = 0;
        m_agentState.clear();
        m_clockSource.clear();
    }
}

void WsprClient::start(int pollSec)
{
    if (pollSec < kMinPollSec) pollSec = kMinPollSec;
    m_pollSec = pollSec;
    if (!m_url.isValid() || m_url.host().isEmpty()) {
        m_lastError = QStringLiteral("no agent host configured");
        emit pollCompleted(0, m_lastError);
        return;
    }
    m_pollTimer->start(pollSec * 1000);
    poll();   // fire one immediately rather than waiting for the first tick
}

void WsprClient::stop()
{
    m_pollTimer->stop();
    if (m_inFlight) {
        m_inFlight->abort();
        m_inFlight = nullptr;
    }
}

void WsprClient::poll()
{
    if (m_inFlight) return;        // previous request still running — skip
    if (!m_url.isValid()) return;

    QNetworkRequest req(m_url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("ShackBook/0.2 (+https://github.com/nigelfenton/shackbook)"));
    req.setRawHeader("Accept", "application/json");
    // The agent is a plain HTTP service on the local network; a redirect would
    // mean something is intercepting it, so don't follow one.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(15000);

    m_inFlight = m_nam->get(req);
    connect(m_inFlight, &QNetworkReply::finished,
            this, &WsprClient::onReplyFinished);
}

// Static, socket-free: the parse rules live here so they can be tested
// without a radio. onReplyFinished() is a thin wrapper that owns the
// watermark and the signals.
QVector<SpotData> WsprClient::spotsFromStatus(const QByteArray& json, qint64 sinceT,
                                              qint64* newestT, QString* error,
                                              QString* state, QString* clockSource)
{
    QVector<SpotData> out;
    if (newestT) *newestT = 0;
    if (error) error->clear();
    if (state) state->clear();
    if (clockSource) clockSource->clear();

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QString("JSON parse: %1").arg(pe.errorString());
        return out;
    }
    const QJsonObject root = doc.object();

    // Guard against pointing this at some other HTTP service on the LAN:
    // parse nothing unless the document identifies itself as the agent.
    if (root.value(QStringLiteral("agent")).toString() != QLatin1String("AetherWSPR")) {
        if (error) *error = QStringLiteral("not an AetherWSPR agent (no agent:\"AetherWSPR\")");
        return out;
    }

    if (state) *state = root.value(QStringLiteral("state")).toString();
    if (clockSource) *clockSource = root.value(QStringLiteral("clock_source")).toString();

    // sinceT == 0 means "priming": learn the newest timestamp but emit
    // nothing, so connecting does not replay the whole ring.
    const bool priming = (sinceT == 0);
    qint64 newest = sinceT;

    const QJsonArray spots = root.value(QStringLiteral("spots")).toArray();
    for (const auto& v : spots) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        const qint64 t = o.value(QStringLiteral("t")).toVariant().toLongLong();
        if (t > newest) newest = t;
        if (priming || t <= sinceT) continue;   // priming, or already seen

        SpotData s;
        s.call = o.value(QStringLiteral("call")).toString().trimmed().toUpper();
        // WSPR type-2/3: grid present, callsign hashed and never transmitted.
        // A callsign-keyed spot panel has nothing to show for these.
        if (s.call.isEmpty()) continue;

        const double hz = o.value(QStringLiteral("hz")).toVariant().toDouble();
        if (hz <= 0.0) continue;
        s.freqMhz = hz / 1'000'000.0;
        s.mode    = QStringLiteral("WSPR");

        // The decode's own numbers, which are what make a WSPR spot worth
        // seeing: how weak it was, and how little power carried it.
        QStringList parts;
        const int snr      = o.value(QStringLiteral("snr")).toVariant().toInt();
        const QString grid = o.value(QStringLiteral("grid")).toString().trimmed();
        const int drift    = o.value(QStringLiteral("drift")).toVariant().toInt();
        const int dbm      = o.value(QStringLiteral("dbm")).toVariant().toInt();
        parts << QString("%1 dB").arg(snr);
        if (!grid.isEmpty()) parts << grid;
        if (dbm > 0) {
            // dBm -> watts, so "37 dBm" reads as the 5 W it is.
            const double w = std::pow(10.0, (dbm - 30) / 10.0);
            parts << (w < 1.0 ? QString("%1 mW").arg(qRound(w * 1000.0))
                              : QString("%1 W").arg(w, 0, 'g', 2));
        }
        if (drift != 0) parts << QString("drift %1").arg(drift);
        s.comment = parts.join(QStringLiteral(" · "));

        s.source      = QStringLiteral("WSPR");
        s.receivedAt  = QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC);
        s.lifetimeSec = kWsprSpotLifetimeSec;
        out.append(s);
    }

    if (newestT) *newestT = newest;
    return out;
}

void WsprClient::onReplyFinished()
{
    QNetworkReply* reply = m_inFlight;
    m_inFlight = nullptr;
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
        emit pollCompleted(0, m_lastError);
        return;
    }

    qint64 newest = 0;
    QString err, state, clock;
    const QVector<SpotData> spots =
        spotsFromStatus(reply->readAll(), m_lastSpotT, &newest, &err, &state, &clock);

    if (!err.isEmpty()) {
        m_lastError = err;
        emit pollCompleted(0, m_lastError);
        return;
    }
    m_lastError.clear();

    if (state != m_agentState || clock != m_clockSource) {
        m_agentState  = state;
        m_clockSource = clock;
        emit agentStateChanged(m_agentState, m_clockSource);
    }

    for (const SpotData& s : spots) emit spotReceived(s);
    m_lastSpotT = newest;
    emit pollCompleted(static_cast<int>(spots.size()), QString{});
}
} // namespace ShackBook
