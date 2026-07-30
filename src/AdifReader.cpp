#include "AdifReader.h"

#include "LogbookModel.h"

namespace ShackLog {
namespace Adif {

bool nextRecord(const QByteArray& data, qsizetype& pos,
                QHash<QString, QString>* fields)
{
    fields->clear();
    while (pos < data.size()) {
        const qsizetype lt = data.indexOf('<', pos);
        if (lt < 0) break;
        const qsizetype gt = data.indexOf('>', lt + 1);
        if (gt < 0) break;
        const QByteArray spec = data.mid(lt + 1, gt - lt - 1);
        pos = gt + 1;

        // Markers: <EOR> completes a record; <EOH> ends the file header —
        // anything accumulated before it was header metadata, not QSO data.
        const QByteArray marker = spec.trimmed().toUpper();
        if (marker == "EOR") return true;
        if (marker == "EOH") { fields->clear(); continue; }

        // Field spec: TAG:length  or  TAG:length:datatype
        const qsizetype colon1 = spec.indexOf(':');
        if (colon1 < 0) continue;
        const QString name = QString::fromUtf8(spec.left(colon1))
                                 .trimmed().toLower();
        QByteArray rest = spec.mid(colon1 + 1);
        const qsizetype colon2 = rest.indexOf(':');
        if (colon2 >= 0) rest = rest.left(colon2);
        bool ok = false;
        qsizetype len = rest.trimmed().toLongLong(&ok);
        if (!ok || len < 0) continue;
        if (len > data.size() - pos) len = data.size() - pos;  // truncated file
        const QString value =
            QString::fromUtf8(data.constData() + pos, len).trimmed();
        if (!name.isEmpty() && !value.isEmpty())
            fields->insert(name, value);
        pos += len;
    }
    return false;  // no complete record left
}

Qso qsoFromFields(const QHash<QString, QString>& f)
{
    Qso q;
    q.call         = f.value(QStringLiteral("call")).toUpper();
    q.qsoDate      = f.value(QStringLiteral("qso_date"));
    q.timeOn       = f.value(QStringLiteral("time_on"));
    q.timeOff      = f.value(QStringLiteral("time_off"));
    q.band         = f.value(QStringLiteral("band")).toLower();
    q.freq         = f.value(QStringLiteral("freq")).toDouble();
    q.mode         = f.value(QStringLiteral("mode")).toUpper();
    q.submode      = f.value(QStringLiteral("submode")).toUpper();
    q.rstSent      = f.value(QStringLiteral("rst_sent"));
    q.rstRcvd      = f.value(QStringLiteral("rst_rcvd"));
    q.gridsquare   = f.value(QStringLiteral("gridsquare"));
    q.name         = f.value(QStringLiteral("name"));
    q.qth          = f.value(QStringLiteral("qth"));
    q.country      = f.value(QStringLiteral("country"));
    q.state        = f.value(QStringLiteral("state"));
    q.cnty         = f.value(QStringLiteral("cnty"));
    q.cont         = f.value(QStringLiteral("cont"));
    q.dxcc         = f.value(QStringLiteral("dxcc")).toInt();
    q.cqz          = f.value(QStringLiteral("cqz")).toInt();
    q.ituz         = f.value(QStringLiteral("ituz")).toInt();
    q.myCall       = f.value(QStringLiteral("station_callsign"));
    q.myGridsquare = f.value(QStringLiteral("my_gridsquare"));
    q.myState      = f.value(QStringLiteral("my_state"));
    q.txPwr        = f.value(QStringLiteral("tx_pwr")).toDouble();
    q.myOperator   = f.value(QStringLiteral("operator"));
    q.contestId    = f.value(QStringLiteral("contest_id"));
    q.srx          = f.value(QStringLiteral("srx")).toInt();
    q.stx          = f.value(QStringLiteral("stx")).toInt();
    q.srxString    = f.value(QStringLiteral("srx_string"));
    q.stxString    = f.value(QStringLiteral("stx_string"));
    q.comment      = f.value(QStringLiteral("comment"));
    q.notes        = f.value(QStringLiteral("notes"));
    q.qslSent      = f.value(QStringLiteral("qsl_sent"));
    q.qslRcvd      = f.value(QStringLiteral("qsl_rcvd"));
    q.lotwSent     = f.value(QStringLiteral("lotw_qsl_sent"));
    q.lotwRcvd     = f.value(QStringLiteral("lotw_qsl_rcvd"));
    q.lotwSdate    = f.value(QStringLiteral("lotw_qslsdate"));
    q.lotwRdate    = f.value(QStringLiteral("lotw_qslrdate"));
    q.eqslSent     = f.value(QStringLiteral("eqsl_qsl_sent"));
    q.eqslRcvd     = f.value(QStringLiteral("eqsl_qsl_rcvd"));

    // Deprecated MODE forms: ADIF-2-era files (and some loggers, N3FJP
    // included in places) write MODE:USB / MODE:LSB; ADIF 3 wants
    // MODE:SSB + SUBMODE.
    if (q.mode == QLatin1String("USB") || q.mode == QLatin1String("LSB")) {
        q.submode = q.mode;
        q.mode    = QStringLiteral("SSB");
    }
    if (q.band.isEmpty() && q.freq > 0.0)
        q.band = LogbookModel::bandFromFreqMhz(q.freq);
    return q;
}

} // namespace Adif
} // namespace ShackLog
