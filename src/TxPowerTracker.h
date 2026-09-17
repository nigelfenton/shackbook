#pragma once

// TxPowerTracker — "what power did I actually transmit with?" (#23).
//
// A QSO used to be logged with the fixed DEFAULT_TX_PWR from Settings, so a
// 5 W QRP contact made on a rig normally run at 100 W was logged as 100 W.
// A TCI server that measures forward power (AetherSDR sends
// `tx_sensors:trx,mic_dbm,fwd_watts,peak_watts,swr,...;` while transmitting)
// lets the log carry the measured value instead.
//
// This class only decides WHICH number to log. It holds no socket and reads
// no clock, so the rules can be tested without a radio: every call takes the
// caller's monotonic time in milliseconds.
//
// The rules:
//   * A transmission runs from trx on to trx off. Its value is the PEAK
//     forward power seen during it — not the average, which on SSB or CW
//     reflects gaps between words and characters rather than the power
//     setting. On FT8/RTTY peak and average are the same thing anyway.
//   * The QSO gets the most recent transmission's peak, but only if that
//     transmission is still going or ended within the window (2 minutes by
//     default). Older than that, it is more likely to belong to a previous
//     contact than this one, and the caller falls back to the default.
//   * A transmission the server reports as a TUNE carrier is never used —
//     a 10 W tune is not the power a contact was made at. (Only servers that
//     send `tune:` can say so; AetherSDR does not, see TciClient.)
//   * Servers that send sensor readings but no `trx:` edges still work:
//     readings arriving close together are treated as one transmission.

#include <QtGlobal>

#include <optional>

namespace ShackBook {

class TxPowerTracker {
public:
    static constexpr qint64 kDefaultWindowMs = 120000;
    // Readings further apart than this, with no trx edges from the server,
    // are treated as separate transmissions.
    static constexpr qint64 kOrphanGapMs     = 5000;

    void setTransmitting(bool on, qint64 nowMs);
    void setTuning(bool on);
    void addForwardPower(double watts, qint64 nowMs);

    // Peak forward power of the most recent usable transmission, or nothing
    // when there is none recent enough to trust.
    std::optional<double> recentPeakWatts(qint64 nowMs,
                                          qint64 windowMs = kDefaultWindowMs) const;

    // Forget everything (e.g. the connection dropped, or a different radio).
    void reset();

    // ADIF TX_PWR precision: 0.1 W below 10 W (QRP detail matters there),
    // whole watts above.
    static double roundForLog(double watts);

private:
    void finishCurrent(qint64 nowMs);

    bool   m_transmitting{false};
    bool   m_tuning{false};

    // The transmission in progress (trx on).
    bool   m_curIsTune{false};
    bool   m_curHasReading{false};
    double m_curPeak{0.0};

    // The last finished transmission worth logging.
    bool   m_lastValid{false};
    double m_lastPeak{0.0};
    qint64 m_lastEndMs{0};
};

} // namespace ShackBook
