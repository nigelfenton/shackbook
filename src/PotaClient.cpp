#include "PotaClient.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace ShackBook {

PotaClient::PotaClient(QObject* parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this)),
      m_pollTimer(new QTimer(this))
{
    connect(m_pollTimer, &QTimer::timeout, this, &PotaClient::poll);
}

PotaClient::~PotaClient() = default;

bool PotaClient::active() const
{
    return m_pollTimer && m_pollTimer->isActive();
}

void PotaClient::start(int pollSec)
{
    if (pollSec < 5) pollSec = 5;
    m_pollTimer->start(pollSec * 1000);
    poll();   // fire one immediately rather than waiting for the first tick
}

void PotaClient::stop()
{
    m_pollTimer->stop();
    if (m_inFlight) {
        m_inFlight->abort();
        m_inFlight = nullptr;
    }
}

void PotaClient::poll()
{
    if (m_inFlight) return;        // previous request still running — skip

    QNetworkRequest req(m_url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("ShackBook/0.2 (+https://github.com/nigelfenton/shackbook)"));
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    m_inFlight = m_nam->get(req);
    connect(m_inFlight, &QNetworkReply::finished,
            this, &PotaClient::onReplyFinished);
}

void PotaClient::onReplyFinished()
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

    const QByteArray body = reply->readAll();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
        m_lastError = QString("JSON parse: %1").arg(pe.errorString());
        emit pollCompleted(0, m_lastError);
        return;
    }
    m_lastError.clear();

    int count = 0;
    const QJsonArray arr = doc.array();
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        SpotData s;
        s.call    = o.value("activator").toString().trimmed().toUpper();
        const double freqKhz = o.value("frequency").toVariant().toDouble();
        s.freqMhz = freqKhz / 1000.0;
        s.mode    = o.value("mode").toString().trimmed().toUpper();

        // Build a comment that's actually useful in the log: park ref,
        // park name, plus any spotter comments POTA carried.
        QStringList parts;
        const QString ref      = o.value("reference").toString().trimmed();
        const QString parkName = o.value("name").toString().trimmed();
        const QString locDesc  = o.value("locationDesc").toString().trimmed();
        const QString cmts     = o.value("comments").toString().trimmed();
        if (!ref.isEmpty())      parts << ref;
        if (!parkName.isEmpty()) parts << parkName;
        if (!locDesc.isEmpty())  parts << locDesc;
        if (!cmts.isEmpty())     parts << cmts;
        s.comment = parts.join(" — ");

        s.source = QStringLiteral("POTA");

        // POTA spots include spotTime (ISO-8601) and expire (also ISO).
        // Use spotTime for receivedAt so the index doesn't think a 30-min-old
        // spot is brand new; cap lifetime at 30 min from spotTime.
        const QString spotTime = o.value("spotTime").toString().trimmed();
        const QDateTime ts = QDateTime::fromString(spotTime, Qt::ISODate);
        s.receivedAt = ts.isValid() ? ts.toUTC()
                                    : QDateTime::currentDateTimeUtc();
        s.lifetimeSec = 1800;

        if (s.call.isEmpty() || s.freqMhz <= 0.0) continue;
        emit spotReceived(s);
        ++count;
    }
    emit pollCompleted(count, QString{});
}

} // namespace ShackBook
