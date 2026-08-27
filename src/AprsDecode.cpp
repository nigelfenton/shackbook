#include "AprsDecode.h"

#include <QtGlobal>
#include <cmath>

namespace ShackBook {
namespace Aprs {

namespace {

// KISS special bytes
constexpr char FEND  = char(0xC0);
constexpr char FESC  = char(0xDB);
constexpr char TFEND = char(0xDC);
constexpr char TFESC = char(0xDD);

// AX.25 constants
constexpr char AX25_UI  = char(0x03); // UI frame control byte
constexpr char AX25_PID_NOL3 = char(0xF0); // no layer-3 protocol

// Decode one AX.25 address (7 bytes: 6 shifted ASCII + 1 SSID byte) into
// "CALL-SSID" (or "CALL" when SSID 0). Returns false on obviously bad input.
// The final flag in the SSID byte marks the last address in the field.
bool decodeAddress(const char* p, QString* call, bool* last, bool* hasBeenRepeated)
{
    QString c;
    for (int i = 0; i < 6; ++i) {
        const char ch = char((quint8(p[i]) >> 1) & 0x7F);
        if (ch != ' ')
            c.append(ch);
    }
    const quint8 ssidByte = quint8(p[6]);
    const int ssid = (ssidByte >> 1) & 0x0F;
    if (last)             *last = (ssidByte & 0x01) != 0;
    if (hasBeenRepeated)  *hasBeenRepeated = (ssidByte & 0x80) != 0; // the 'H' bit
    if (c.isEmpty())
        return false;
    *call = (ssid > 0) ? QStringLiteral("%1-%2").arg(c).arg(ssid) : c;
    return true;
}

// Encode a "CALL-SSID" string into a 7-byte AX.25 address. `lastAddr` sets the
// end-of-field flag. Returns false if the call won't fit (>6 chars base).
bool encodeAddress(const QString& callSsid, bool lastAddr, QByteArray* out)
{
    QString base = callSsid.trimmed().toUpper();
    int ssid = 0;
    const int dash = base.indexOf('-');
    if (dash >= 0) {
        ssid = base.mid(dash + 1).toInt();
        base = base.left(dash);
    }
    if (base.isEmpty() || base.size() > 6 || ssid < 0 || ssid > 15)
        return false;
    for (int i = 0; i < 6; ++i) {
        const char ch = (i < base.size()) ? base.at(i).toLatin1() : ' ';
        out->append(char((quint8(ch) << 1) & 0xFE));
    }
    quint8 ssidByte = 0x60 | (quint8(ssid) << 1); // reserved bits = 1, then SSID
    if (lastAddr) ssidByte |= 0x01;
    out->append(char(ssidByte));
    return true;
}

// Parse an APRS lat/lon from the fixed 8+9-char uncompressed position form,
// e.g. "5132.07N/00007.53W". Returns false if it doesn't match.
bool parseUncompressedPosition(const QString& s, double* lat, double* lon,
                               char* symTable, char* symCode)
{
    // "DDMM.mmN" "/" "DDDMM.mmW" symbol table char is at index 8, code at 18
    if (s.size() < 19)
        return false;
    bool ok = false;
    const double latDeg = s.mid(0, 2).toDouble(&ok);         if (!ok) return false;
    const double latMin = s.mid(2, 5).toDouble(&ok);         if (!ok) return false;
    const QChar  latNS  = s.at(7);
    *symTable = s.at(8).toLatin1();
    const double lonDeg = s.mid(9, 3).toDouble(&ok);         if (!ok) return false;
    const double lonMin = s.mid(12, 5).toDouble(&ok);        if (!ok) return false;
    const QChar  lonEW  = s.at(17);
    *symCode = s.at(18).toLatin1();

    double la = latDeg + latMin / 60.0;
    if (latNS == 'S' || latNS == 's') la = -la;
    double lo = lonDeg + lonMin / 60.0;
    if (lonEW == 'W' || lonEW == 'w') lo = -lo;
    if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0)
        return false;
    *lat = la; *lon = lo;
    return true;
}

// APRS interprets the info field by its first char (Data Type Identifier).
void parseApresInfo(Report* r)
{
    if (r->info.isEmpty())
        return;
    r->dataType = r->info.at(0).toLatin1();

    switch (r->dataType) {
    case '!': case '=': {
        // Position without timestamp. Payload starts right after the DTI.
        // (A leading '!' may appear later in the info for Ultimeter etc., but
        //  the common case is DTI at index 0.)
        const QString body = r->info.mid(1);
        double la, lo; char st, sc;
        if (parseUncompressedPosition(body, &la, &lo, &st, &sc)) {
            r->hasPosition = true;
            r->latitude = la; r->longitude = lo;
            r->symbolTable = st; r->symbolCode = sc;
            r->comment = body.mid(19).trimmed();
        }
        break;
    }
    case '@': case '/': {
        // Position WITH timestamp: 7-char timestamp then the position. Skip the
        // DTI (1) + timestamp (7).
        const QString body = r->info.mid(8);
        double la, lo; char st, sc;
        if (parseUncompressedPosition(body, &la, &lo, &st, &sc)) {
            r->hasPosition = true;
            r->latitude = la; r->longitude = lo;
            r->symbolTable = st; r->symbolCode = sc;
            r->comment = body.mid(19).trimmed();
        }
        break;
    }
    case ':': {
        // Message: ":ADDRESSEE:text{msgno" — addressee is 9 chars, then ':'.
        if (r->info.size() < 11 || r->info.at(10) != ':')
            break;
        r->hasMessage = true;
        r->addressee = r->info.mid(1, 9).trimmed().toUpper();
        QString rest = r->info.mid(11);
        // ack / rej: text is "ackNNNNN" or "rejNNNNN"
        if (rest.startsWith(QLatin1String("ack")) || rest.startsWith(QLatin1String("rej"))) {
            r->isAck = true;
            r->messageNo = rest.mid(3).trimmed();
            break;
        }
        const int brace = rest.indexOf('{');
        if (brace >= 0) {
            r->messageNo = rest.mid(brace + 1).trimmed();
            rest = rest.left(brace);
        }
        r->messageText = rest;
        break;
    }
    default:
        break; // status, telemetry, object, etc. — not decoded (yet)
    }
}

} // namespace

// --- KISS ---------------------------------------------------------------
QVector<QByteArray> kissUnframe(const QByteArray& in, QByteArray* leftover)
{
    QVector<QByteArray> out;
    QByteArray cur;
    bool inFrame = false;
    bool esc = false;
    int lastFend = -1;

    for (int i = 0; i < in.size(); ++i) {
        const char b = in.at(i);
        if (b == FEND) {
            if (inFrame && !cur.isEmpty()) {
                // cur[0] is the KISS type byte; low nibble 0 = data frame.
                if ((quint8(cur.at(0)) & 0x0F) == 0x00)
                    out.append(cur.mid(1));
            }
            cur.clear();
            inFrame = true;
            esc = false;
            lastFend = i;
            continue;
        }
        if (!inFrame)
            continue;
        if (esc) {
            cur.append(b == TFEND ? FEND : b == TFESC ? FESC : b);
            esc = false;
        } else if (b == FESC) {
            esc = true;
        } else {
            cur.append(b);
        }
    }
    // Anything after the last FEND is an incomplete frame — hand it back.
    if (leftover) {
        *leftover = (lastFend >= 0) ? in.mid(lastFend) : in;
        // if we ended exactly on a FEND boundary with a clean frame, drop it
        if (!inFrame) leftover->clear();
    }
    return out;
}

QByteArray kissFrameData(const QByteArray& ax25)
{
    QByteArray out;
    out.append(FEND);
    out.append(char(0x00)); // type byte: data, port 0
    for (const char b : ax25) {
        if (b == FEND)      { out.append(FESC); out.append(TFEND); }
        else if (b == FESC) { out.append(FESC); out.append(TFESC); }
        else                { out.append(b); }
    }
    out.append(FEND);
    return out;
}

// --- AX.25 + APRS -------------------------------------------------------
std::optional<Report> decodeAx25(const QByteArray& ax25)
{
    // Minimum: dest(7) + src(7) + control(1) + pid(1) = 16 bytes.
    if (ax25.size() < 16)
        return std::nullopt;

    const char* p = ax25.constData();
    Report r;

    QString dest, src;
    bool last = false;
    if (!decodeAddress(p, &dest, &last, nullptr))     return std::nullopt;
    int off = 7;
    if (!decodeAddress(p + off, &src, &last, nullptr)) return std::nullopt;
    off += 7;

    // Digipeater path (0..8 addresses), until the end-of-address flag.
    QStringList path;
    while (!last && off + 7 <= ax25.size() && path.size() < 8) {
        QString hop; bool rpt = false;
        if (!decodeAddress(p + off, &hop, &last, &rpt))
            break;
        path << (rpt ? hop + QStringLiteral("*") : hop);
        off += 7;
    }

    if (off + 2 > ax25.size())
        return std::nullopt;
    const char control = ax25.at(off);
    const char pid     = ax25.at(off + 1);
    off += 2;

    r.source = src;
    r.destination = dest;
    r.path = path;

    // Only UI frames with no-layer-3 PID carry APRS.
    if (control != AX25_UI || pid != AX25_PID_NOL3)
        return r; // still a valid (addressed) frame, just not APRS info

    r.info = QString::fromLatin1(ax25.mid(off));
    parseApresInfo(&r);
    return r;
}

QByteArray buildAx25Ui(const QString& source, const QString& dest,
                       const QStringList& path, const QByteArray& info)
{
    QByteArray out;
    // Address field: dest, source, then path. Last address gets the end flag.
    const bool haveHops = !path.isEmpty();
    if (!encodeAddress(dest, false, &out))                 return {};
    if (!encodeAddress(source, !haveHops, &out))           return {};
    for (int i = 0; i < path.size(); ++i) {
        QString hop = path.at(i);
        hop.remove('*');
        if (!encodeAddress(hop, i == path.size() - 1, &out))
            return {};
    }
    out.append(AX25_UI);
    out.append(AX25_PID_NOL3);
    out.append(info);
    return out;
}

QByteArray buildMessageInfo(const QString& addressee, const QString& text,
                            const QString& msgNo)
{
    QString a = addressee.trimmed().toUpper().left(9);
    while (a.size() < 9) a.append(' ');
    QString info = QStringLiteral(":%1:%2").arg(a, text);
    if (!msgNo.isEmpty())
        info += QStringLiteral("{%1").arg(msgNo);
    return info.toLatin1();
}

} // namespace Aprs
} // namespace ShackBook
