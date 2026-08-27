#include "HttpServer.h"

#include "LogbookModel.h"
#include "Qso.h"

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace ShackBook::Server {

namespace {

// ───────────────────────── HTTP helpers ─────────────────────────

QByteArray buildResponse(int status,
                         const QByteArray& reason,
                         const QByteArray& contentType,
                         const QByteArray& body)
{
    QByteArray r;
    r.reserve(160 + body.size());
    r += "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    r += "Content-Type: " + contentType + "\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Connection: close\r\n";
    r += "Server: ShackBook/0.1 (phase-1c)\r\n";
    r += "\r\n";
    r += body;
    return r;
}

QByteArray jsonResp(int status, const QByteArray& reason, const QJsonObject& obj)
{
    return buildResponse(status, reason, "application/json",
                         QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QByteArray jsonError(int status, const QByteArray& reason, const QString& msg)
{
    QJsonObject o;
    o["error"] = msg;
    return jsonResp(status, reason, o);
}

// Parse one HTTP request — request line + headers — from `raw` up to the
// blank line at `hdrEnd`.  Header keys are lower-cased for easy lookup.
struct ParsedRequest {
    QByteArray method;
    QByteArray fullPath;   // path with optional ?query
    QHash<QString, QString> headers;
};

ParsedRequest parseHead(const QByteArray& raw, int hdrEnd)
{
    ParsedRequest p;
    const QByteArray head = raw.left(hdrEnd);
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty()) return p;
    const QByteArray reqLine = lines.first().trimmed();
    const int sp1 = reqLine.indexOf(' ');
    const int sp2 = sp1 >= 0 ? reqLine.indexOf(' ', sp1 + 1) : -1;
    if (sp1 >= 0 && sp2 >= 0) {
        p.method   = reqLine.left(sp1);
        p.fullPath = reqLine.mid(sp1 + 1, sp2 - sp1 - 1);
    }
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty()) continue;
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QString name  = QString::fromLatin1(line.left(colon)).trimmed().toLower();
        const QString value = QString::fromLatin1(line.mid(colon + 1)).trimmed();
        p.headers.insert(name, value);
    }
    return p;
}

// ───────────────────────── Qso ↔ JSON ─────────────────────────

QJsonObject qsoToJson(const Qso& q)
{
    QJsonObject o;
    o["id"]            = static_cast<qint64>(q.id);
    o["call"]          = q.call;
    o["qso_date"]      = q.qsoDate;
    o["time_on"]       = q.timeOn;
    if (!q.timeOff.isEmpty())     o["time_off"]     = q.timeOff;
    o["band"]          = q.band;
    o["mode"]          = q.mode;
    if (q.freq > 0.0)             o["freq"]         = q.freq;
    if (!q.submode.isEmpty())     o["submode"]      = q.submode;
    if (!q.rstSent.isEmpty())     o["rst_sent"]     = q.rstSent;
    if (!q.rstRcvd.isEmpty())     o["rst_rcvd"]     = q.rstRcvd;
    if (!q.name.isEmpty())        o["name"]         = q.name;
    if (!q.qth.isEmpty())         o["qth"]          = q.qth;
    if (!q.gridsquare.isEmpty())  o["gridsquare"]   = q.gridsquare;
    if (q.dxcc)                   o["dxcc"]         = q.dxcc;
    if (!q.country.isEmpty())     o["country"]      = q.country;
    if (!q.state.isEmpty())       o["state"]        = q.state;
    if (!q.contestId.isEmpty())   o["contest_id"]   = q.contestId;
    if (q.srx)                    o["srx"]          = q.srx;
    if (q.stx)                    o["stx"]          = q.stx;
    if (!q.station.isEmpty())     o["station"]      = q.station;
    if (!q.myCall.isEmpty())      o["my_call"]      = q.myCall;
    if (q.txPwr > 0.0)            o["tx_pwr"]       = q.txPwr;
    if (!q.comment.isEmpty())     o["comment"]      = q.comment;
    if (!q.notes.isEmpty())       o["notes"]        = q.notes;
    if (!q.createdAt.isEmpty())   o["created_at"]   = q.createdAt;
    if (!q.updatedAt.isEmpty())   o["updated_at"]   = q.updatedAt;
    return o;
}

// ───────────────────────── ARRL Field Day scoring ─────────────────────────
// Score = (QSO points × power multiplier) + bonus points.
//   QSO points: phone = 1, CW = 2, digital = 2 (per ARRL FD rules).
//     We classify by N3FJP's MODETEST bucket (stashed in Qso::submode): PH / CW / DG.
//     Falls back to the operating MODE string if submode is absent.
//   Power multiplier (station setting, passed in): 5 (≤5W non-commercial),
//     2 (≤100W / ≤5W mains), 1 (>100W). Default 2.
//   Bonus points: checkbox-based, not derivable from QSOs — passed in (default 0).
// Returns a JSON object with the breakdown.
QJsonObject computeFdScore(const QVector<Qso>& qsos, int powerMult, int bonusPoints)
{
    auto bucket = [](const Qso& q) -> QString {
        // Prefer MODETEST (submode): PH / CW / DG. Else infer from MODE.
        const QString mt = q.submode.toUpper();
        if (mt == "PH" || mt == "CW" || mt == "DG") return mt;
        const QString m = q.mode.toUpper();
        if (m == "CW") return "CW";
        if (m == "SSB" || m == "AM" || m == "FM" || m == "PH" || m == "PHONE" ||
            m == "USB" || m == "LSB") return "PH";
        // everything else (FT8, FT4, RTTY, PSK, MFSK, DIGITAL, DATA...) = digital
        return "DG";
    };

    int nPhone = 0, nCw = 0, nDig = 0;
    for (const auto& q : qsos) {
        const QString b = bucket(q);
        if (b == "PH") ++nPhone;
        else if (b == "CW") ++nCw;
        else ++nDig;
    }
    const int qsoPoints = nPhone * 1 + nCw * 2 + nDig * 2;
    const int mult = (powerMult >= 1 && powerMult <= 5) ? powerMult : 2;
    const int total = qsoPoints * mult + bonusPoints;

    QJsonObject byMode;
    byMode["phone"]   = nPhone;
    byMode["cw"]      = nCw;
    byMode["digital"] = nDig;

    QJsonObject o;
    o["qso_count"]      = nPhone + nCw + nDig;
    o["qsos_by_class"]  = byMode;
    o["qso_points"]     = qsoPoints;       // phone×1 + (cw+dig)×2
    o["power_multiplier"] = mult;
    o["bonus_points"]   = bonusPoints;
    o["total_score"]    = total;
    o["formula"]        = QStringLiteral("(%1 qso pts x %2) + %3 bonus = %4")
                              .arg(qsoPoints).arg(mult).arg(bonusPoints).arg(total);
    return o;
}

// Construct a Qso from JSON.  Required fields:
//   call, qso_date (YYYYMMDD), time_on (HHMM or HHMMSS), band, mode
// Anything else is optional.  `missing` collects the names of any required
// fields that were absent, so the caller can return a useful 400 body.
Qso qsoFromJson(const QJsonObject& o, QStringList& missing)
{
    Qso q;
    auto needStr = [&](const char* k, QString& dst) {
        const QJsonValue v = o.value(QString::fromLatin1(k));
        if (!v.isString() || v.toString().trimmed().isEmpty()) {
            missing << QString::fromLatin1(k);
        } else {
            dst = v.toString().trimmed();
        }
    };
    auto optStr = [&](const char* k, QString& dst) {
        const QJsonValue v = o.value(QString::fromLatin1(k));
        if (v.isString()) dst = v.toString();
    };
    auto optInt = [&](const char* k, int& dst) {
        const QJsonValue v = o.value(QString::fromLatin1(k));
        if (v.isDouble()) dst = v.toInt();
    };
    auto optDouble = [&](const char* k, double& dst) {
        const QJsonValue v = o.value(QString::fromLatin1(k));
        if (v.isDouble()) dst = v.toDouble();
    };

    needStr("call",     q.call);
    needStr("qso_date", q.qsoDate);
    needStr("time_on",  q.timeOn);
    needStr("band",     q.band);
    needStr("mode",     q.mode);

    optStr ("time_off",     q.timeOff);
    optDouble("freq",       q.freq);
    optStr ("submode",      q.submode);
    optStr ("rst_sent",     q.rstSent);
    optStr ("rst_rcvd",     q.rstRcvd);
    optStr ("name",         q.name);
    optStr ("qth",          q.qth);
    optStr ("gridsquare",   q.gridsquare);
    optInt ("dxcc",         q.dxcc);
    optStr ("country",      q.country);
    optStr ("state",        q.state);
    optStr ("cnty",         q.cnty);
    optStr ("cont",         q.cont);
    optInt ("cqz",          q.cqz);
    optInt ("ituz",         q.ituz);
    optStr ("my_call",      q.myCall);
    optStr ("my_gridsquare",q.myGridsquare);
    optStr ("my_state",     q.myState);
    optDouble("tx_pwr",     q.txPwr);
    optStr ("my_operator",  q.myOperator);
    optStr ("contest_id",   q.contestId);
    optInt ("srx",          q.srx);
    optInt ("stx",          q.stx);
    optStr ("srx_string",   q.srxString);
    optStr ("stx_string",   q.stxString);
    optStr ("station",      q.station);
    optStr ("comment",      q.comment);
    optStr ("notes",        q.notes);
    return q;
}

} // namespace

// ───────────────────────── HttpServer ─────────────────────────

HttpServer::HttpServer(LogbookModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &HttpServer::onNewConnection);
}

bool HttpServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "HttpServer: bind to port" << port << "failed:"
                   << m_server->errorString();
        return false;
    }
    qInfo() << "HttpServer: listening on port" << m_server->serverPort();
    return true;
}

quint16 HttpServer::port() const            { return m_server->serverPort(); }
bool    HttpServer::isListening() const     { return m_server->isListening(); }

void HttpServer::onNewConnection()
{
    while (auto* client = m_server->nextPendingConnection()) {
        connect(client, &QTcpSocket::disconnected,
                client, &QTcpSocket::deleteLater);

        connect(client, &QTcpSocket::readyRead, client, [client, this] {
            // Accumulate incoming bytes on the socket via a dynamic property
            // so we can wait for the full body (POST may arrive in pieces).
            QByteArray buf = client->property("buf").toByteArray();
            buf += client->readAll();

            const int hdrEnd = buf.indexOf("\r\n\r\n");
            if (hdrEnd < 0) {
                client->setProperty("buf", buf);  // header still incomplete
                return;
            }

            const ParsedRequest req = parseHead(buf, hdrEnd);
            const int  bodyStart    = hdrEnd + 4;
            const int  contentLen   = req.headers.value("content-length", "0").toInt();
            const int  haveBody     = buf.size() - bodyStart;
            if (haveBody < contentLen) {
                client->setProperty("buf", buf);  // body still incomplete
                return;
            }
            const QByteArray body = buf.mid(bodyStart, contentLen);
            client->setProperty("buf", QByteArray());  // request complete

            // Dispatch.
            const QUrl   url(QString::fromUtf8(req.fullPath));
            const QString path = url.path();
            const QUrlQuery qs(url);

            QByteArray response;

            // ─── GET / ───
            if (req.method == "GET" && path == "/") {
                response = buildResponse(
                    200, "OK", "text/plain; charset=utf-8",
                    "ShackBook Server alive\n");
            }
            // ─── GET /api/state ───
            else if (req.method == "GET" && path == "/api/state") {
                QJsonObject state;
                state["status"]   = "alive";
                state["version"]  = "0.1.0-phase1c";
                state["phase"]    = QStringLiteral("1c");
                if (m_model && m_model->isOpen()) {
                    state["db_path"]   = m_model->databasePath();
                    state["qso_count"] = m_model->countQsos();
                } else {
                    state["db_path"]   = QJsonValue();
                    state["qso_count"] = 0;
                    state["db_error"]  = "logbook not open";
                }
                response = jsonResp(200, "OK", state);
            }
            // ─── GET /api/qsos ───
            else if (req.method == "GET" && path == "/api/qsos") {
                if (!m_model || !m_model->isOpen()) {
                    response = jsonError(503, "Service Unavailable",
                                         QStringLiteral("logbook not open"));
                } else {
                    LogbookFilter f;
                    bool ok = false;
                    const int lim = qs.queryItemValue("limit").toInt(&ok);
                    f.limit = (ok && lim > 0) ? qMin(lim, 1000) : 50;
                    const auto rows = m_model->queryQsos(f);
                    QJsonArray arr;
                    for (const auto& q : rows) arr.append(qsoToJson(q));
                    QJsonObject body;
                    body["count"] = static_cast<int>(rows.size());
                    body["limit"] = f.limit;
                    body["qsos"]  = arr;
                    response = jsonResp(200, "OK", body);
                }
            }
            // ─── GET /api/score ───  ARRL Field Day live score
            else if (req.method == "GET" && path == "/api/score") {
                if (!m_model || !m_model->isOpen()) {
                    response = jsonError(503, "Service Unavailable",
                                         QStringLiteral("logbook not open"));
                } else {
                    // power multiplier + bonus points are station settings passed in
                    // as query params (?mult=2&bonus=400); sensible FD defaults otherwise.
                    bool okM = false, okB = false;
                    const int mult  = qs.queryItemValue("mult").toInt(&okM);
                    const int bonus = qs.queryItemValue("bonus").toInt(&okB);
                    LogbookFilter f;            // limit 0 == all QSOs
                    const auto rows = m_model->queryQsos(f);
                    response = jsonResp(200, "OK",
                        computeFdScore(rows, okM ? mult : 2, okB ? bonus : 0));
                }
            }
            // ─── POST /api/qsos ───
            else if (req.method == "POST" && path == "/api/qsos") {
                if (!m_model || !m_model->isOpen()) {
                    response = jsonError(503, "Service Unavailable",
                                         QStringLiteral("logbook not open"));
                } else {
                    QJsonParseError perr;
                    const QJsonDocument doc =
                        QJsonDocument::fromJson(body, &perr);
                    if (perr.error != QJsonParseError::NoError ||
                        !doc.isObject()) {
                        response = jsonError(
                            400, "Bad Request",
                            QStringLiteral("invalid JSON: %1").arg(perr.errorString()));
                    } else {
                        QStringList missing;
                        Qso q = qsoFromJson(doc.object(), missing);
                        if (!missing.isEmpty()) {
                            response = jsonError(
                                400, "Bad Request",
                                QStringLiteral("missing required field(s): %1")
                                    .arg(missing.join(", ")));
                        } else if (!m_model->insertQso(q, QStringLiteral("http-api"))) {
                            response = jsonError(
                                500, "Internal Server Error",
                                m_model->errorString());
                        } else {
                            QJsonObject ok;
                            ok["status"] = "created";
                            ok["qso"]    = qsoToJson(q);
                            response = jsonResp(201, "Created", ok);
                        }
                    }
                }
            }
            // ─── 404 ───
            else {
                response = buildResponse(
                    404, "Not Found", "text/plain; charset=utf-8",
                    "Not Found\n");
            }

            client->write(response);
            client->disconnectFromHost();
        });
    }
}

} // namespace ShackBook::Server
