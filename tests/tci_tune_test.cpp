// tci_tune_test — the spot-tuning mode mapping and guards (#9).
//
// This is the first code in the project that WRITES to the radio. Everything
// before it followed the rig; a bug here moves somebody's transceiver in the
// middle of a QSO. Two things are worth pinning:
//
//   1. The ADIF -> TCI mode mapping, and in particular that "SSB" is NOT a
//      sideband. Below 10 MHz it means LSB, above it USB. Getting that wrong
//      puts the radio on the opposite sideband and the DX sounds like
//      nothing at all — a failure the operator would blame on propagation.
//
//   2. That an unknown or ambiguous mode leaves the radio ALONE rather than
//      picking something plausible.
//
// The mapping is a pure function precisely so it can be tested without a
// radio, a socket or a connection.

#include "TciClient.h"

#include <QCoreApplication>

#include <cstdio>

using namespace ShackBook;

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) { ++g_failures; std::printf("FAIL: %s\n", what); }
    else       std::printf("[ OK ] %s\n", what);
}

static void checkMode(const char* adif, double mhz, const char* want)
{
    const QString got = tciModulationForAdifMode(QString::fromLatin1(adif), mhz);
    const bool ok = (got == QString::fromLatin1(want));
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s @ %.3f MHz -> \"%s\", wanted \"%s\"\n",
                    adif, mhz, qPrintable(got), want);
    } else {
        std::printf("[ OK ] %-8s @ %8.3f MHz -> %s\n", adif, mhz,
                    *want ? want : "(nothing — left alone)");
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ── ⭐ SSB resolves by band, and refuses to guess without one ───────
    checkMode("SSB",  3.780, "lsb");     // 80 m — lower sideband
    checkMode("SSB",  7.180, "lsb");     // 40 m
    checkMode("SSB",  9.999, "lsb");     // just under the convention's line
    checkMode("SSB", 10.001, "usb");     // just over it
    checkMode("SSB", 14.250, "usb");     // 20 m — upper
    checkMode("SSB", 28.400, "usb");     // 10 m
    checkMode("SSB",  0.0,   "");        // no frequency known: do NOT guess
    checkMode("SSB", -1.0,   "");        // nor on a nonsense one

    // ── The unambiguous modes ──────────────────────────────────────────
    checkMode("CW",   14.030, "cw");
    checkMode("USB",   3.780, "usb");    // explicit beats the band convention
    checkMode("LSB",  14.250, "lsb");    // even where it is unconventional
    checkMode("AM",    3.885, "am");
    checkMode("FM",   29.600, "nfm");
    checkMode("RTTY", 14.080, "digl");
    checkMode("FT8",  14.074, "digu");
    checkMode("FT4",   7.047, "digu");

    // ── ⭐ Ambiguous or unknown: leave the radio alone ──────────────────
    checkMode("DIGITAL", 14.070, "");    // could be any of a dozen things
    checkMode("DATA",    14.070, "");
    checkMode("",        14.070, "");
    checkMode("   ",     14.070, "");
    checkMode("NONSENSE",14.070, "");

    // ── Case and whitespace, since spot sources are inconsistent ───────
    checkMode("ft8",   14.074, "digu");
    checkMode(" CW ",  14.030, "cw");
    checkMode("Usb",    3.780, "usb");

    // ── Guards on the client itself ────────────────────────────────────
    // A disconnected client must send nothing: the spot list is visible
    // whether or not a radio is attached, so double-clicking with nothing
    // connected is the normal case rather than an edge one.
    {
        TciClient tci;
        check(!tci.connected(), "a fresh client is not connected");
        check(!tci.tuneToMhz(14.074), "tuneToMhz sends nothing while disconnected");
        check(!tci.setModeString("USB"), "setModeString sends nothing while disconnected");
        check(!tci.tuneToMhz(0.0),    "a zero frequency is refused");
        check(!tci.tuneToMhz(-1.0),   "a negative frequency is refused");
        check(!tci.tuneToMhz(1.0e9),  "an absurd frequency is refused");
    }

    if (g_failures == 0) {
        std::printf("tci_tune_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "tci_tune_test: %d failure(s)\n", g_failures);
    return 1;
}
