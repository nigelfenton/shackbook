#include "N3fjpClient.h"

#include "../LogbookModel.h"
#include "../Qso.h"

#include <QTcpSocket>
#include <QTimer>
#include <QDebug>

namespace {

// Extract the text content of a child tag (case-insensitive, flat envelope).
// Same shape as N3fjpServer's helper — kept local so the client is standalone.
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

// N3FJP DATE -> ADIF YYYYMMDD. ENTEREVENT already sends YYYYMMDD; tolerate /,- too.
QString normaliseDate(const QString& in)
{
    QString d = in; d.remove('/').remove('-').remove(' ');
    return d.size() == 8 ? d : QString();
}

// N3FJP TIME -> ADIF HHMM/HHMMSS. ENTEREVENT sends HHMMSS; tolerate colons.
QString normaliseTime(const QString& in)
{
    QString t = in; t.remove(':').remove(' ');
    return (t.size() == 4 || t.size() == 6) ? t : QString();
}

// BAND comes bare in ENTEREVENT ("20"); normalise to ADIF ("20m").
QString normaliseBand(const QString& in)
{
    QString b = in.trimmed().toLower();
    if (b.isEmpty()) return {};
    if (b.endsWith('m') || b.endsWith("cm") || b.endsWith("mm")) return b;
    bool ok = false; (void) b.toDouble(&ok);
    return ok ? b + 'm' : b;
}

} // namespace

namespace ShackBook::Server {

N3fjpClient::N3fjpClient(LogbookModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_socket(new QTcpSocket(this))
    , m_reconnect(new QTimer(this))
{
    m_reconnect->setInterval(5000);
    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, &N3fjpClient::tryReconnect);

    connect(m_socket, &QTcpSocket::connected,    this, &N3fjpClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &N3fjpClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &N3fjpClient::onReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &N3fjpClient::onError);
}

void N3fjpClient::start(const QString& host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_running = true;
    qInfo().noquote() << "N3fjpClient: connecting to N3FJP at" << host << ":" << port;
    m_socket->connectToHost(host, port);
}

void N3fjpClient::stop()
{
    m_running = false;
    m_reconnect->stop();
    m_socket->abort();
}

bool N3fjpClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void N3fjpClient::onConnected()
{
    qInfo() << "N3fjpClient: connected — subscribing (SETUPDATESTATE TRUE)";
    m_buf.clear();
    // Subscribe: N3FJP will then push <ENTEREVENT> on each logged QSO.
    m_socket->write("<CMD><SETUPDATESTATE><VALUE>TRUE</VALUE></CMD>\r\n");
    m_socket->flush();
    emit connectedChanged(true);
}

void N3fjpClient::onDisconnected()
{
    qWarning() << "N3fjpClient: disconnected";
    emit connectedChanged(false);
    if (m_running) m_reconnect->start();   // keep the link up
}

void N3fjpClient::onError(QAbstractSocket::SocketError)
{
    qWarning().noquote() << "N3fjpClient: socket error —" << m_socket->errorString();
    if (m_running && m_socket->state() != QAbstractSocket::ConnectedState)
        m_reconnect->start();
}

void N3fjpClient::tryReconnect()
{
    if (!m_running) return;
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        qInfo() << "N3fjpClient: reconnecting...";
        m_socket->connectToHost(m_host, m_port);
    }
}

void N3fjpClient::onReadyRead()
{
    m_buf += m_socket->readAll();
    // Frames are <CMD>...</CMD> envelopes (CR/LF separated, but we split on the
    // envelope so partial reads and concatenated frames both work).
    forever {
        const int start = m_buf.indexOf("<CMD>");
        if (start < 0) {
            // no frame start yet — drop any leading noise to bound the buffer
            if (m_buf.size() > 65536) m_buf.clear();
            break;
        }
        const int end = m_buf.indexOf("</CMD>", start);
        if (end < 0) break;                       // wait for the rest
        const int frameEnd = end + 6;             // past "</CMD>"
        const QString frame = QString::fromUtf8(m_buf.mid(start, frameEnd - start));
        m_buf.remove(0, frameEnd);
        handleFrame(frame);
    }
}

void N3fjpClient::handleFrame(const QString& frame)
{
    // inner = content between <CMD> and </CMD>
    const int a = frame.indexOf("<CMD>", 0, Qt::CaseInsensitive);
    const int b = frame.lastIndexOf("</CMD>", -1, Qt::CaseInsensitive);
    if (a < 0 || b < 0) return;
    const QString inner = frame.mid(a + 5, b - (a + 5));

    if (inner.startsWith("<ENTEREVENT", Qt::CaseInsensitive)) {
        handleEnterEvent(inner);
    }
    // Other frames (SETUPDATESTATERESPONSE, UPDATERESPONSE, READBMFRESPONSE,
    // ENTERRESPONSE, CALLTABEVENT, ...) are ignored — we only mirror logged QSOs.
}

void N3fjpClient::handleEnterEvent(const QString& inner)
{
    if (!m_model || !m_model->isOpen()) {
        qWarning() << "N3fjpClient ENTEREVENT: logbook not open";
        return;
    }

    Qso q;
    q.call    = tagValue(inner, "CALL").toUpper();
    q.band    = normaliseBand(tagValue(inner, "BAND"));
    // MODE is the operating mode (SSB/CW/FT8...); MODETEST is N3FJP's scoring
    // bucket (PH/CW/DG). Keep MODE; stash MODETEST in submode for scoring.
    q.mode    = tagValue(inner, "MODE").toUpper();
    q.submode = tagValue(inner, "MODETEST").toUpper();
    q.freq    = tagValue(inner, "FREQ").toDouble();
    q.qsoDate = normaliseDate(tagValue(inner, "QSO_DATE"));
    q.timeOn  = normaliseTime(tagValue(inner, "TIME_ON"));
    q.timeOff = q.timeOn;
    q.rstSent = tagValue(inner, "RST_SENT");
    q.rstRcvd = tagValue(inner, "RST_RCVD");
    q.gridsquare = tagValue(inner, "GRIDSQUARE");
    q.country = tagValue(inner, "COUNTRY");
    q.dxcc    = tagValue(inner, "DXCC").toInt();

    // FD exchange: their CLASS + ARRL_SECT. Store in the contest serial-string
    // fields the Qso model already carries (srxString = received exchange).
    const QString cls  = tagValue(inner, "CLASS");
    const QString sect = tagValue(inner, "ARRL_SECT");
    if (!cls.isEmpty() || !sect.isEmpty())
        q.srxString = (cls + ' ' + sect).trimmed();
    q.contestId = QStringLiteral("ARRL-FIELD-DAY");

    if (q.band.isEmpty() && q.freq > 0.0)
        q.band = LogbookModel::bandFromFreqMhz(q.freq);

    if (q.call.isEmpty() || q.qsoDate.isEmpty() || q.band.isEmpty() || q.mode.isEmpty()) {
        qWarning().noquote() << "N3fjpClient ENTEREVENT: incomplete QSO, skipped —" << inner.left(120);
        return;
    }

    if (!m_model->insertQso(q, QStringLiteral("n3fjp-client"))) {
        qWarning().noquote() << "N3fjpClient ENTEREVENT insert failed —" << m_model->errorString();
        return;
    }
    ++m_mirrored;
    qInfo().noquote() << QString("N3fjpClient mirrored QSO #%1 — id=%2 call=%3 %4 %5 (%6)")
                         .arg(m_mirrored).arg(q.id).arg(q.call, q.band, q.mode, q.submode);
    emit qsoMirrored(q.id, q.call);
}

} // namespace ShackBook::Server
