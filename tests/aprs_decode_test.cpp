// Unit tests for AprsDecode — KISS framing, AX.25 addressing, and APRS
// position/message parsing. Canned frames were generated independently (a
// Python encoder) from the AX.25 + APRS specs, so this cross-checks the C++
// decoder against a separate implementation. No radio / socket needed.

#include "AprsDecode.h"

#include <QByteArray>
#include <QCoreApplication>

#include <cmath>
#include <cstdio>

using namespace ShackBook::Aprs;

namespace {

int g_failed = 0;
int g_total = 0;

void report(const char* label, bool ok)
{
    ++g_total;
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label);
    if (!ok) ++g_failed;
}

// Canned frames (hex, no FCS) built by an independent Python encoder.
QByteArray fromHex(const char* h) { return QByteArray::fromHex(h); }

const char* kPosKiss =
    "c00082a0b4606062608e6094969c4072ae92888a62406303f021353133322e30374e2f30303030"
    "372e3533573e54657374203730636d206d6f62696c65c0";
const char* kPosAx25 =
    "82a0b4606062608e6094969c4072ae92888a62406303f021353133322e30374e2f30303030372e"
    "3533573e54657374203730636d206d6f62696c65";
const char* kMsgAx25 =
    "82a0b4606062608e6094969c4072ae92888a62406303f03a5733414243202020203a6869207468"
    "6572657b3432";

bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // --- KISS unframing ---
    {
        const QByteArray payloads = fromHex(kPosKiss);
        const auto frames = kissUnframe(payloads);
        report("KISS: one data frame extracted", frames.size() == 1);
        report("KISS: payload matches the AX.25 frame",
               !frames.isEmpty() && frames.first() == fromHex(kPosAx25));
    }

    // --- KISS round-trip (build then unframe) ---
    {
        const QByteArray ax25 = fromHex(kPosAx25);
        const QByteArray kiss = kissFrameData(ax25);
        const auto back = kissUnframe(kiss);
        report("KISS: frame() then unframe() round-trips",
               back.size() == 1 && back.first() == ax25);
    }

    // --- KISS escaping (payload containing FEND/FESC) ---
    {
        QByteArray raw;
        raw.append(char(0xC0)); raw.append('X'); raw.append(char(0xDB)); raw.append('Y');
        const auto back = kissUnframe(kissFrameData(raw));
        report("KISS: FEND/FESC bytes survive escape round-trip",
               back.size() == 1 && back.first() == raw);
    }

    // --- AX.25 decode: addressing + path ---
    {
        const auto r = decodeAx25(fromHex(kPosAx25));
        report("AX25: decodes", r.has_value());
        if (r) {
            report("AX25: source = G0JKN-9",  r->source == QStringLiteral("G0JKN-9"));
            report("AX25: dest   = APZ001",   r->destination == QStringLiteral("APZ001"));
            report("AX25: path   = [WIDE1-1]",
                   r->path.size() == 1 && r->path.first() == QStringLiteral("WIDE1-1"));
        }
    }

    // --- APRS position report ---
    {
        const auto r = decodeAx25(fromHex(kPosAx25));
        report("APRS: position decoded", r && r->hasPosition);
        if (r && r->hasPosition) {
            report("APRS: lat ~ 51.5345 N", near(r->latitude, 51.5345, 1e-3));
            report("APRS: lon ~ -0.1255 W", near(r->longitude, -0.1255, 1e-3));
            report("APRS: symbol '/>' (car)",
                   r->symbolTable == '/' && r->symbolCode == '>');
            report("APRS: comment carried",
                   r->comment == QStringLiteral("Test 70cm mobile"));
        }
    }

    // --- APRS message ---
    {
        const auto r = decodeAx25(fromHex(kMsgAx25));
        report("APRS: message decoded", r && r->hasMessage);
        if (r && r->hasMessage) {
            report("APRS: addressee = W3ABC",  r->addressee == QStringLiteral("W3ABC"));
            report("APRS: text = 'hi there'",  r->messageText == QStringLiteral("hi there"));
            report("APRS: msgNo = 42",         r->messageNo == QStringLiteral("42"));
            report("APRS: not an ack",         !r->isAck);
        }
    }

    // --- TX build round-trips through decode ---
    {
        const QByteArray info = buildMessageInfo(QStringLiteral("W3ABC"),
                                                 QStringLiteral("hello"),
                                                 QStringLiteral("7"));
        const QByteArray frame = buildAx25Ui(QStringLiteral("G0JKN-9"),
                                             QStringLiteral("APZ001"),
                                             {QStringLiteral("WIDE1-1")}, info);
        report("TX: buildAx25Ui produced a frame", !frame.isEmpty());
        const auto r = decodeAx25(frame);
        report("TX: our built frame decodes back to the message",
               r && r->hasMessage
                 && r->source == QStringLiteral("G0JKN-9")
                 && r->addressee == QStringLiteral("W3ABC")
                 && r->messageText == QStringLiteral("hello")
                 && r->messageNo == QStringLiteral("7"));
    }

    // --- robustness: junk in, no crash, no false positive ---
    {
        report("robust: empty frame -> nullopt",   !decodeAx25(QByteArray()).has_value());
        report("robust: short frame -> nullopt",   !decodeAx25(QByteArray(10, 'x')).has_value());
        report("robust: empty KISS -> no frames",  kissUnframe(QByteArray()).isEmpty());
    }

    if (g_failed == 0) {
        std::printf("\nAll %d APRS-decode tests passed.\n", g_total);
        return 0;
    }
    std::printf("\n%d of %d APRS-decode tests failed.\n", g_failed, g_total);
    return 1;
}
