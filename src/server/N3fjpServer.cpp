#include "N3fjpServer.h"

#include "LogbookModel.h"
#include "Qso.h"

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTcpSocket>

namespace ShackBook::Server {

namespace {

constexpr const char* kProgramName     = "ShackBook";
constexpr const char* kProgramVersion  = "0.1.0-phase1c";
constexpr const char* kCompatApiLabel  = "N3FJP-compat-2.2";

// Strip <CMD>...</CMD> envelope and return the inner content, or empty
// QString if the frame is malformed.
QString unwrapCmd(const QString& frame)
{
    QString s = frame.trimmed();
    if (!s.startsWith(QLatin1String("<CMD>"), Qt::CaseInsensitive) ||
        !s.endsWith(QLatin1String("</CMD>"),  Qt::CaseInsensitive))
        return {};
    return s.mid(5, s.size() - 5 - 6);  // strip 5 open + 6 close chars
}

// Extract the command tag (first <TAG>) from the unwrapped content.
QString commandTag(const QString& inner)
{
    const int lt = inner.indexOf('<');
    const int gt = inner.indexOf('>', lt + 1);
    if (lt < 0 || gt < 0) return {};
    return inner.mid(lt + 1, gt - lt - 1).toUpper();
}

// Extract the text content of a child tag.  Case-insensitive on the tag
// name.  Returns empty if not found.  Naive — does not handle nested
// tags of the same name, escaping, or self-closing tags.  Good enough
// for the flat N3FJP envelope shape.
QString tagValue(const QString& inner, const QString& tagName)
{
    const QString open  = '<'  + tagName;
    const QString close = "</" + tagName + '>';
    const int openIdx = inner.indexOf(open, 0, Qt::CaseInsensitive);
    if (openIdx < 0) return {};
    const int gt = inner.indexOf('>', openIdx);
    if (gt < 0) return {};
    const int closeIdx = inner.indexOf(close, gt, Qt::CaseInsensitive);
    if (closeIdx < 0) return {};
    return inner.mid(gt + 1, closeIdx - gt - 1).trimmed();
}

// Build the standard <CMD>...</CMD> response envelope.
QString cmd(const QString& body)
{
    return "<CMD>" + body + "</CMD>";
}

// ───────────── WSJT-X / N3FJP date/time normalisation ─────────────

// N3FJP DATE: "YYYY/MM/DD" — normalise to ADIF "YYYYMMDD".
QString normaliseDate(const QString& in)
{
    QString d = in;
    d.remove('/').remove('-').remove(' ');
    return d.size() == 8 ? d : QString();
}

// N3FJP TIMEON/TIMEOFF: "HH:MM" — normalise to ADIF "HHMM".  Already
// HHMMSS or HHMM is passed through if it looks valid.
QString normaliseTime(const QString& in)
{
    QString t = in;
    t.remove(':').remove(' ');
    if (t.size() == 4 || t.size() == 6) return t;
    return {};
}

// N3FJP BAND is sometimes the bare number ("20", "40") and sometimes the
// ADIF form ("20m", "40m").  Normalise to ADIF.
QString normaliseBand(const QString& in)
{
    QString b = in.trimmed().toLower();
    if (b.isEmpty()) return {};
    if (b.endsWith('m') || b.endsWith("cm") || b.endsWith("mm")) return b;
    // bare number? assume metres
    bool ok = false;
    (void) b.toDouble(&ok);
    if (ok) return b + 'm';
    return b;
}

} // namespace

// ───────────────────────── N3fjpServer ─────────────────────────

N3fjpServer::N3fjpServer(LogbookModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &N3fjpServer::onNewConnection);
}

bool N3fjpServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "N3fjpServer: bind to port" << port << "failed:"
                   << m_server->errorString();
        return false;
    }
    qInfo() << "N3fjpServer: listening on port" << m_server->serverPort();
    return true;
}

quint16 N3fjpServer::port() const           { return m_server->serverPort(); }
bool    N3fjpServer::isListening() const    { return m_server->isListening(); }

void N3fjpServer::onNewConnection()
{
    while (auto* client = m_server->nextPendingConnection()) {
        const QString peer = client->peerAddress().toString();
        client->setProperty("peer", peer);
        qInfo() << "N3fjpServer: client connected from" << peer;

        connect(client, &QTcpSocket::readyRead,
                this,   &N3fjpServer::onSocketReadyRead);
        connect(client, &QTcpSocket::disconnected,
                client, &QTcpSocket::deleteLater);
    }
}

void N3fjpServer::onSocketReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Accumulate until we have a full CR+LF-terminated frame.
    QByteArray buf = socket->property("buf").toByteArray();
    buf += socket->readAll();

    for (;;) {
        const int eol = buf.indexOf("\r\n");
        if (eol < 0) break;
        const QByteArray frameBytes = buf.left(eol);
        buf.remove(0, eol + 2);

        // A bare CR+LF is the documented disconnect signal.
        if (frameBytes.isEmpty()) {
            qInfo() << "N3fjpServer: client requested disconnect";
            socket->disconnectFromHost();
            socket->setProperty("buf", QByteArray());
            return;
        }

        const QString frame = QString::fromUtf8(frameBytes);
        const QString peer  = socket->property("peer").toString();
        const QString response = handleFrame(frame, peer);
        if (!response.isEmpty()) {
            socket->write(response.toUtf8() + "\r\n");
        }
    }
    socket->setProperty("buf", buf);
}

QString N3fjpServer::handleFrame(const QString& frame, const QString& clientId)
{
    const QString inner = unwrapCmd(frame);
    if (inner.isEmpty()) {
        qDebug() << "N3fjpServer: malformed frame ignored:" << frame;
        return {};
    }
    const QString tag = commandTag(inner);

    if (tag == QLatin1String("PROGRAM")) {
        return cmd(QStringLiteral(
            "<PROGRAMRESPONSE>"
            "<PGM>%1</PGM>"
            "<VER>%2</VER>"
            "<APIVER>%3</APIVER>")
            .arg(QLatin1String(kProgramName),
                 QLatin1String(kProgramVersion),
                 QLatin1String(kCompatApiLabel)));
    }

    if (tag == QLatin1String("APIVER")) {
        return cmd(QStringLiteral(
            "<APIVERRESPONSE><APIVER>%1</APIVER>")
            .arg(QLatin1String(kCompatApiLabel)));
    }

    if (tag == QLatin1String("UPDATEANDLOG")) {
        return handleUpdateAndLog(inner, clientId);
    }

    // Unsupported commands — politely tell the client so it doesn't hang.
    qDebug() << "N3fjpServer: unsupported command:" << tag;
    return cmd(QStringLiteral(
        "<UNSUPPORTED><COMMAND>%1</COMMAND>"
        "<REASON>not implemented in Phase 1c — coming in 1d/1e</REASON>")
        .arg(tag));
}

QString N3fjpServer::handleUpdateAndLog(const QString& inner,
                                        const QString& clientId)
{
    if (!m_model || !m_model->isOpen()) {
        qWarning() << "N3fjpServer UPDATEANDLOG: logbook not open";
        return cmd(QStringLiteral(
            "<UPDATEANDLOGFAIL><REASON>logbook not open</REASON>"));
    }

    Qso q;
    q.call       = tagValue(inner, "CALL").toUpper();
    q.qsoDate    = normaliseDate(tagValue(inner, "DATE"));
    q.timeOn     = normaliseTime(tagValue(inner, "TIMEON"));
    q.timeOff    = normaliseTime(tagValue(inner, "TIMEOFF"));
    q.band       = normaliseBand(tagValue(inner, "BAND"));
    q.mode       = tagValue(inner, "MODE").toUpper();
    q.freq       = tagValue(inner, "FREQ").toDouble();
    q.rstSent    = tagValue(inner, "RSTS");
    q.rstRcvd    = tagValue(inner, "RSTR");
    q.gridsquare = tagValue(inner, "GRID");
    q.txPwr      = tagValue(inner, "POWER").toDouble();

    // If BAND wasn't supplied but we have a frequency, derive it.
    if (q.band.isEmpty() && q.freq > 0.0)
        q.band = LogbookModel::bandFromFreqMhz(q.freq);

    QStringList missing;
    if (q.call.isEmpty())    missing << "CALL";
    if (q.qsoDate.isEmpty()) missing << "DATE";
    if (q.timeOn.isEmpty())  missing << "TIMEON";
    if (q.band.isEmpty())    missing << "BAND";
    if (q.mode.isEmpty())    missing << "MODE";
    if (!missing.isEmpty()) {
        const QString reason = QString("missing/invalid: %1").arg(missing.join(", "));
        qWarning().noquote() << "N3fjpServer UPDATEANDLOG rejected —" << reason;
        return cmd(QStringLiteral(
            "<UPDATEANDLOGFAIL><REASON>%1</REASON>").arg(reason));
    }

    const QString actor = QStringLiteral("n3fjp:%1").arg(clientId);
    if (!m_model->insertQso(q, actor)) {
        const QString reason = m_model->errorString();
        qWarning().noquote() << "N3fjpServer UPDATEANDLOG insert failed —" << reason;
        return cmd(QStringLiteral(
            "<UPDATEANDLOGFAIL><REASON>%1</REASON>").arg(reason));
    }

    qInfo().noquote() << QString("N3fjpServer UPDATEANDLOG ok — id=%1 call=%2 band=%3 mode=%4 actor=%5")
                         .arg(q.id).arg(q.call, q.band, q.mode, actor);

    // The N3FJP spec does not define an UPDATEANDLOG-response shape, so
    // real N3FJP-aware tools (notably WSJT-X) don't wait for one.  Stay
    // silent on success — WSJT-X fires and forgets.  (Errors above DO
    // return an envelope so test tooling sees failures explicitly.)
    return {};
}

} // namespace ShackBook::Server
