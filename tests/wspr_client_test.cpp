// WSPR spot parsing: WsprClient::spotsFromStatus.
//
// The agent re-sends its ENTIRE spot ring (up to 2000 entries) on every poll,
// so the only thing stopping a reconnect from replaying hours of history into
// the spot panel is the watermark in this function. That, and two shapes of
// real agent data that look like corruption and are not:
//
//   * `call` is legitimately EMPTY for WSPR type-2/3 messages, where the
//     6-character grid is sent and the callsign is hashed rather than
//     transmitted. 5 of 575 spots in a 14-hour run on a FLEX-6300 were these.
//   * `snr` is a WSPR SNR in dB and is NORMALLY NEGATIVE (-31..0). A sign
//     error or an unsigned read would silently mangle every spot.
//
// Socket-free: no radio, no network, no QNetworkAccessManager.

#include "WsprClient.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

#include <cstdio>

using namespace ShackBook;

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++failures;
}

// A minimal but realistic agent document. Field names and value shapes are
// copied from a live FLEX-6300 running AetherWSPR 0.1.0.
QByteArray statusDoc(const QByteArray& spotsArray,
                     const QByteArray& agent = "AetherWSPR",
                     const QByteArray& state = "capturing")
{
    return "{\"agent\":\"" + agent + "\",\"version\":\"0.1.0\",\"state\":\"" + state
         + "\",\"clock_source\":\"radio_ntp\",\"band\":\"40m\",\"dial_hz\":7038600,"
           "\"spots\":[" + spotsArray + "]}";
}

// One spot, in the agent's own field order.
QByteArray spot(qint64 t, const char* call, const char* grid,
                int snr, int hz, int drift, int dbm)
{
    return QByteArray("{\"t\":") + QByteArray::number(t)
         + ",\"band\":\"40m\",\"snr\":" + QByteArray::number(snr)
         + ",\"dt\":-0.4,\"hz\":" + QByteArray::number(hz)
         + ",\"drift\":" + QByteArray::number(drift)
         + ",\"call\":\"" + call + "\",\"grid\":\"" + grid
         + "\",\"dbm\":" + QByteArray::number(dbm) + "}";
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QByteArray three =
        spot(1000, "W4VIR",  "EM72", -21, 7040000, 0, 37) + ","
      + spot(1120, "N4TRB",  "EM73", -25, 7040103, -1, 33) + ","
      + spot(1240, "KB8VME", "EN61", -20, 7040030, 0, 23);

    // ── Priming: the reason this function takes a watermark ──────────────
    // sinceT == 0 is the first poll after connecting. It must learn the
    // newest timestamp and emit NOTHING. If this regresses, every reconnect
    // dumps the agent's whole ring into the panel at once.
    {
        qint64 newest = -1;
        QString err;
        const auto out = WsprClient::spotsFromStatus(statusDoc(three), 0, &newest, &err);
        check(err.isEmpty(), "priming poll parses without error");
        check(out.isEmpty(), "priming emits NO spots (would otherwise flood the panel)");
        check(newest == 1240, "priming still learns the newest timestamp");
    }

    // ── The watermark actually filters ───────────────────────────────────
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(statusDoc(three), 1000, &newest, &err);
        check(out.size() == 2, "a later poll emits only spots newer than the watermark");
        check(!out.isEmpty() && out.first().call == QLatin1String("N4TRB"),
              "the spot AT the watermark is treated as already seen");
        check(newest == 1240, "the watermark advances to the newest seen");
    }
    {
        // Re-polling with nothing new must emit nothing, not re-emit the tail.
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(statusDoc(three), 1240, &newest, &err);
        check(out.isEmpty(), "re-polling an unchanged window emits nothing");
        check(newest == 1240, "the watermark holds when nothing is new");
    }

    // ── Hashed callsigns: real data that looks like corruption ───────────
    // WSPR type-2/3 carries a 6-char grid and NO callsign. Skipped, because
    // a callsign-keyed spot panel has nothing to show. Asserted so a future
    // "tidy up the empty-string check" cannot turn these into blank rows.
    {
        const QByteArray mixed =
            spot(2000, "",      "JN61TP", -30, 7040042, 0, 23) + ","
          + spot(2120, "W4EO",  "FM29",   -13, 7040045, 0, 20);
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(statusDoc(mixed), 1999, &newest, &err);
        check(out.size() == 1, "a hashed-call spot is skipped, the named one is kept");
        check(!out.isEmpty() && out.first().call == QLatin1String("W4EO"),
              "the surviving spot is the one with a real callsign");
        check(newest == 2120,
              "a skipped spot still advances the watermark (or it repeats forever)");
    }

    // ── Negative SNR must survive ────────────────────────────────────────
    // -31..0 dB is the normal range for WSPR. Reading it unsigned, or losing
    // the sign in the comment, would misreport every single spot.
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(
            statusDoc(spot(3000, "AJ8S", "EM89", -31, 7040055, 0, 37)), 2999, &newest, &err);
        check(out.size() == 1 && out.first().comment.startsWith(QLatin1String("-31 dB")),
              "a -31 dB SNR is reported as negative, not mangled");
    }

    // ── dBm is rendered as power an operator reads at a glance ───────────
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(
            statusDoc(spot(4000, "K1ABC", "FN42", -20, 7040100, 0, 23) + ","
                    + spot(4120, "K2DEF", "FN43", -20, 7040101, 0, 37)),
            3999, &newest, &err);
        check(out.size() == 2, "both power levels parse");
        check(!out.isEmpty() && out.first().comment.contains(QLatin1String("200 mW")),
              "23 dBm renders as 200 mW");
        check(out.size() > 1 && out.at(1).comment.contains(QLatin1String("5 W")),
              "37 dBm renders as 5 W");
    }

    // ── Drift only appears when non-zero ────────────────────────────────
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(
            statusDoc(spot(5000, "K3GHI", "FM19", -18, 7040019, 1, 23)), 4999, &newest, &err);
        check(out.size() == 1 && out.first().comment.contains(QLatin1String("drift 1")),
              "a non-zero drift is shown");
    }
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(
            statusDoc(spot(5120, "K4JKL", "FM19", -18, 7040019, 0, 23)), 5119, &newest, &err);
        check(out.size() == 1 && !out.first().comment.contains(QLatin1String("drift")),
              "zero drift is omitted rather than shown as 'drift 0'");
    }

    // ── Frequency: the spot's own Hz, not the dial ───────────────────────
    // hz sits inside the 200 Hz WSPR window, ~1500 Hz above dial_hz. Using
    // dial_hz would put every spot on the same frequency.
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(
            statusDoc(spot(6000, "K5MNO", "EM12", -22, 7040000, 0, 23)), 5999, &newest, &err);
        check(out.size() == 1 && qAbs(out.first().freqMhz - 7.040000) < 1e-9,
              "freqMhz comes from the spot's hz, not the dial frequency");
        check(!out.isEmpty() && out.first().mode == QLatin1String("WSPR"),
              "mode is WSPR");
        check(!out.isEmpty() && out.first().source == QLatin1String("WSPR"),
              "source is WSPR so the panel can tell the feed apart");
    }

    // ── The agent guard ─────────────────────────────────────────────────
    // Pointing this at another HTTP service on the LAN must fail loudly
    // rather than parse whatever JSON happens to be there.
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(
            statusDoc(three, "SomethingElse"), 1000, &newest, &err);
        check(out.isEmpty(), "a non-AetherWSPR document yields no spots");
        check(!err.isEmpty(), "and reports an error rather than failing silently");
    }

    // ── Malformed input must not crash or invent spots ──────────────────
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus("not json at all", 0, &newest, &err);
        check(out.isEmpty() && !err.isEmpty(), "garbage input errors, no spots");
    }
    {
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus("[1,2,3]", 0, &newest, &err);
        check(out.isEmpty() && !err.isEmpty(), "a JSON array (not object) errors");
    }
    {
        // An empty window is normal: the agent serves this before it decodes
        // anything. It must be quiet, not an error.
        qint64 newest = -1;
        QString err;
        const auto out = WsprClient::spotsFromStatus(statusDoc(""), 0, &newest, &err);
        check(out.isEmpty() && err.isEmpty(), "an empty spot list is quiet, not an error");
        check(newest == 0, "an empty window leaves the watermark at 0");
    }
    {
        // hz missing or zero would otherwise yield a 0 MHz spot.
        qint64 newest = 0;
        QString err;
        const auto out = WsprClient::spotsFromStatus(
            statusDoc("{\"t\":7000,\"call\":\"K6PQR\",\"grid\":\"DM04\",\"snr\":-20,"
                      "\"hz\":0,\"drift\":0,\"dbm\":23}"), 6999, &newest, &err);
        check(out.isEmpty(), "a spot with hz=0 is dropped rather than logged at 0 MHz");
    }

    // ── State and clock source reach the caller ─────────────────────────
    // "waiting_clock" is the state an operator needs told about: on a
    // FLEX-6000 it is permanent until the clock gate is satisfied, and
    // nothing else in the UI explains why no spots arrive.
    {
        qint64 newest = 0;
        QString err, state, clock;
        WsprClient::spotsFromStatus(statusDoc("", "AetherWSPR", "waiting_clock"),
                                    0, &newest, &err, &state, &clock);
        check(state == QLatin1String("waiting_clock"), "agent state is reported");
        check(clock == QLatin1String("radio_ntp"), "clock source is reported");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
