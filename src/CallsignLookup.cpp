#include "CallsignLookup.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QXmlStreamReader>

namespace ShackBook {

namespace {
// Collect every leaf element's text, keyed by lower-cased element name.
// QRZ and HamQTH both nest their payload (<HamQTH><session><session_id>…),
// so this walks tokens rather than using readElementText() — calling that
// on a container element consumes its whole subtree and returns nothing,
// which silently discarded every response until 2026-06-12.
QHash<QString, QString> flatXml(const QByteArray& body)
{
    QHash<QString, QString> out;
    QXmlStreamReader xml(body);
    QString name, text;
    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement:
            name = xml.name().toString().toLower();
            text.clear();
            break;
        case QXmlStreamReader::Characters:
            if (!xml.isWhitespace()) text += xml.text();
            break;
        case QXmlStreamReader::EndElement:
            // Only leaves store: a container's EndElement arrives after a
            // child already cleared `name`, so containers fall through.
            if (!name.isEmpty() && !text.trimmed().isEmpty())
                out.insert(name, text.trimmed());
            name.clear();
            text.clear();
            break;
        default:
            break;
        }
    }
    return out;
}
} // namespace

CallsignLookup::CallsignLookup(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void CallsignLookup::configure(Provider provider, const QString& user,
                               const QString& password, bool callookForUs)
{
    if (provider != m_provider || user != m_user || password != m_pass) {
        m_qrzKey.clear();             // credentials changed → drop sessions
        m_hamqthSession.clear();
    }
    m_provider     = provider;
    m_user         = user;
    m_pass         = password;
    m_callookForUs = callookForUs;
}

QNetworkReply* CallsignLookup::get(const QString& url)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("ShackBook"));
    return m_nam->get(req);
}

void CallsignLookup::lookup(const QString& callIn, bool isUsCall)
{
    const QString call = callIn.trimmed().toUpper();
    if (call.isEmpty()) return;

    switch (m_provider) {
    case Provider::Qrz:
        if (m_user.isEmpty() || m_pass.isEmpty()) return;
        if (m_qrzKey.isEmpty()) qrzLogin(call);
        else                    qrzQuery(call, /*retry*/ true);
        return;
    case Provider::HamQth:
        if (m_user.isEmpty() || m_pass.isEmpty()) return;
        if (m_hamqthSession.isEmpty()) hamqthLogin(call);
        else                           hamqthQuery(call, /*retry*/ true);
        return;
    case Provider::None:
        if (m_callookForUs && isUsCall) callookQuery(call);
        return;
    }
}

// ── QRZ XML (paid subscription) ────────────────────────────────────────────

void CallsignLookup::qrzLogin(const QString& pendingCall)
{
    auto* rep = get(QStringLiteral(
        "https://xmldata.qrz.com/xml/current/?username=%1;password=%2;agent=ShackBook")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(m_user)),
             QString::fromUtf8(QUrl::toPercentEncoding(m_pass))));
    connect(rep, &QNetworkReply::finished, this, [this, rep, pendingCall] {
        rep->deleteLater();
        const auto f = flatXml(rep->readAll());
        m_qrzKey = f.value(QStringLiteral("key"));
        if (m_qrzKey.isEmpty()) {
            Result r; r.call = pendingCall; r.source = "QRZ";
            r.error = f.value(QStringLiteral("error"),
                              QStringLiteral("QRZ login failed"));
            emit result(r);
            return;
        }
        qrzQuery(pendingCall, /*retry*/ false);
    });
}

void CallsignLookup::qrzQuery(const QString& call, bool retryOnExpiry)
{
    auto* rep = get(QStringLiteral(
        "https://xmldata.qrz.com/xml/current/?s=%1;callsign=%2")
        .arg(m_qrzKey, QString::fromUtf8(QUrl::toPercentEncoding(call))));
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, call, retryOnExpiry] {
        rep->deleteLater();
        const auto f = flatXml(rep->readAll());

        const QString err = f.value(QStringLiteral("error"));
        if (!err.isEmpty()) {
            if (retryOnExpiry && (err.contains("Session", Qt::CaseInsensitive)
                               || err.contains("expired", Qt::CaseInsensitive))) {
                m_qrzKey.clear();
                qrzLogin(call);           // one re-login, then give up
                return;
            }
            Result r; r.call = call; r.source = "QRZ"; r.error = err;
            emit result(r);
            return;
        }

        Result r;
        r.call    = call;
        r.source  = "QRZ";
        r.ok      = true;
        const QString fname = f.value(QStringLiteral("fname"));
        const QString lname = f.value(QStringLiteral("name"));
        r.name    = (fname + QLatin1Char(' ') + lname).trimmed();
        r.qth     = f.value(QStringLiteral("addr2"));
        r.state   = f.value(QStringLiteral("state"));
        r.county  = f.value(QStringLiteral("county"));
        r.country = f.value(QStringLiteral("country"));
        r.grid    = f.value(QStringLiteral("grid"));
        r.dxcc    = f.value(QStringLiteral("dxcc")).toInt();
        emit result(r);
    });
}

// ── HamQTH (free account) ──────────────────────────────────────────────────

void CallsignLookup::hamqthLogin(const QString& pendingCall)
{
    auto* rep = get(QStringLiteral(
        "https://www.hamqth.com/xml.php?u=%1&p=%2")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(m_user)),
             QString::fromUtf8(QUrl::toPercentEncoding(m_pass))));
    connect(rep, &QNetworkReply::finished, this, [this, rep, pendingCall] {
        rep->deleteLater();
        const auto f = flatXml(rep->readAll());
        m_hamqthSession = f.value(QStringLiteral("session_id"));
        if (m_hamqthSession.isEmpty()) {
            Result r; r.call = pendingCall; r.source = "HamQTH";
            r.error = f.value(QStringLiteral("error"),
                              QStringLiteral("HamQTH login failed"));
            emit result(r);
            return;
        }
        hamqthQuery(pendingCall, /*retry*/ false);
    });
}

void CallsignLookup::hamqthQuery(const QString& call, bool retryOnExpiry)
{
    auto* rep = get(QStringLiteral(
        "https://www.hamqth.com/xml.php?id=%1&callsign=%2&prg=ShackBook")
        .arg(m_hamqthSession,
             QString::fromUtf8(QUrl::toPercentEncoding(call))));
    connect(rep, &QNetworkReply::finished, this,
            [this, rep, call, retryOnExpiry] {
        rep->deleteLater();
        const auto f = flatXml(rep->readAll());

        const QString err = f.value(QStringLiteral("error"));
        if (!err.isEmpty()) {
            if (retryOnExpiry && err.contains("session", Qt::CaseInsensitive)) {
                m_hamqthSession.clear();
                hamqthLogin(call);
                return;
            }
            Result r; r.call = call; r.source = "HamQTH"; r.error = err;
            emit result(r);
            return;
        }

        Result r;
        r.call    = call;
        r.source  = "HamQTH";
        r.ok      = true;
        // HamQTH: <nick> is the operator's name; <adr_city>/<qth> the town.
        r.name    = f.value(QStringLiteral("nick"));
        r.qth     = f.value(QStringLiteral("qth"),
                            f.value(QStringLiteral("adr_city")));
        r.state   = f.value(QStringLiteral("us_state"));
        r.county  = f.value(QStringLiteral("us_county"));
        r.country = f.value(QStringLiteral("country"));
        r.grid    = f.value(QStringLiteral("grid"));
        r.dxcc    = f.value(QStringLiteral("adif")).toInt();
        emit result(r);
    });
}

// ── callook.info (US calls, no account) ────────────────────────────────────

void CallsignLookup::callookQuery(const QString& call)
{
    auto* rep = get(QStringLiteral("https://callook.info/%1/json")
                        .arg(QString::fromUtf8(QUrl::toPercentEncoding(call))));
    connect(rep, &QNetworkReply::finished, this, [this, rep, call] {
        rep->deleteLater();
        const auto doc = QJsonDocument::fromJson(rep->readAll());
        const auto o = doc.object();

        Result r;
        r.call   = call;
        r.source = "callook";
        if (o.value(QStringLiteral("status")).toString() != QLatin1String("VALID")) {
            r.error = QStringLiteral("not found");
            emit result(r);
            return;
        }
        r.ok   = true;
        r.name = o.value(QStringLiteral("name")).toString();
        // address.line2 = "CITY, ST 12345"
        const QString line2 = o.value(QStringLiteral("address")).toObject()
                               .value(QStringLiteral("line2")).toString();
        const QStringList parts = line2.split(QLatin1Char(','));
        if (!parts.isEmpty()) r.qth = parts[0].trimmed();
        if (parts.size() > 1) {
            const QStringList st =
                parts[1].trimmed().split(QLatin1Char(' '),
                                         Qt::SkipEmptyParts);
            if (!st.isEmpty()) r.state = st[0].trimmed();
        }
        r.grid = o.value(QStringLiteral("location")).toObject()
                  .value(QStringLiteral("gridsquare")).toString();
        r.country = QStringLiteral("United States");
        r.dxcc    = 291;
        emit result(r);
    });
}

} // namespace ShackBook
