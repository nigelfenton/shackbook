#include "N1mmSpotParser.h"

#include <QHash>
#include <QStringList>
#include <QXmlStreamReader>

namespace ShackBook {

QString n1mmStatusFlag(const QString& statusList)
{
    const QStringList tokens = statusList.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // What a contest operator most needs to notice first: a busted call before a
    // dupe, a dupe before a multiplier, and so on. "single mult" and "double mult"
    // both carry "mult"; "new qso" carries "new".
    static const char* const priority[] = {"bust", "dupe", "mult", "cq", "busy", "qtc", "new"};
    for (const char* flag : priority) {
        if (tokens.contains(QLatin1String(flag))) return QString::fromLatin1(flag);
    }
    return {};
}

N1mmParse parseN1mmSpot(const QByteArray& datagram, N1mmSpot& out)
{
    QXmlStreamReader xml(datagram);

    // Find the root element. Anything that never gets there isn't XML at all.
    while (!xml.atEnd() && !xml.isStartElement()) xml.readNext();
    if (!xml.isStartElement()) return N1mmParse::Malformed;

    // N1MM sends several document types to the same port. Only <spot> is ours;
    // the others are expected traffic, not errors.
    if (xml.name().compare(QLatin1String("spot"), Qt::CaseInsensitive) != 0)
        return N1mmParse::NotSpot;

    // Element names are matched case-insensitively: N1MM writes <StationName>,
    // other senders don't all agree on case.
    QHash<QString, QString> field;
    while (xml.readNextStartElement()) {
        const QString name = xml.name().toString().toLower();
        field.insert(name, xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
    }

    // ⛔ Read to the end before believing anything. A datagram cut short (UDP
    // fragment loss) can still have yielded a call and the first digits of the
    // frequency: "<frequency>140" would put the spot at 0.140 MHz, on the wrong
    // band. A truncated document is an error here, so it is refused whole.
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) return N1mmParse::Malformed;

    const QString call = field.value(QStringLiteral("dxcall")).toUpper();
    if (call.isEmpty() || call.size() > 20 || call.contains(QLatin1Char(' ')))
        return N1mmParse::Malformed;

    bool ok = false;
    const double khz = field.value(QStringLiteral("frequency")).toDouble(&ok);
    // 1 kHz to 300 GHz, the same plausibility range as the radio clients.
    if (!ok || khz < 1.0 || khz > 3.0e8) return N1mmParse::Malformed;

    QString status = field.value(QStringLiteral("statuslist"));
    if (status.isEmpty()) status = field.value(QStringLiteral("status"));

    const QString station = field.value(QStringLiteral("stationname"));

    out = N1mmSpot{};
    out.spot.call     = call;
    out.spot.freqMhz  = khz / 1000.0;
    out.spot.mode     = field.value(QStringLiteral("mode")).toUpper();
    out.spot.comment  = field.value(QStringLiteral("comment"));
    out.spot.spotter  = field.value(QStringLiteral("spottercall")).toUpper();
    out.spot.status   = n1mmStatusFlag(status);
    out.spot.source   = station.isEmpty() ? QStringLiteral("N1MM")
                                          : QStringLiteral("N1MM: %1").arg(station);
    out.stationName   = station;
    out.remove = field.value(QStringLiteral("action")).compare(QLatin1String("delete"),
                                                               Qt::CaseInsensitive) == 0;
    return N1mmParse::Spot;
}

} // namespace ShackBook
