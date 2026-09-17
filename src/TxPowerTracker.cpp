#include "TxPowerTracker.h"

#include <cmath>

namespace ShackBook {

void TxPowerTracker::setTransmitting(bool on, qint64 nowMs)
{
    if (on == m_transmitting) return;
    if (on) {
        m_transmitting  = true;
        m_curIsTune     = m_tuning;
        m_curHasReading = false;
        m_curPeak       = 0.0;
    } else {
        finishCurrent(nowMs);
        m_transmitting = false;
    }
}

void TxPowerTracker::setTuning(bool on)
{
    m_tuning = on;
    // A tune that starts mid-transmission still taints it: the peak would be
    // the tune carrier's, not the contact's.
    if (on && m_transmitting) m_curIsTune = true;
}

void TxPowerTracker::addForwardPower(double watts, qint64 nowMs)
{
    if (!std::isfinite(watts) || watts <= 0.0) return;   // keyed but no RF yet, or garbage

    if (m_transmitting) {
        if (!m_curHasReading || watts > m_curPeak) m_curPeak = watts;
        m_curHasReading = true;
        return;
    }

    // No trx edges from this server: group readings that arrive close
    // together into one transmission.
    if (m_tuning) return;
    if (!m_lastValid || nowMs - m_lastEndMs > kOrphanGapMs) {
        m_lastPeak = watts;
    } else if (watts > m_lastPeak) {
        m_lastPeak = watts;
    }
    m_lastValid = true;
    m_lastEndMs = nowMs;
}

void TxPowerTracker::finishCurrent(qint64 nowMs)
{
    if (m_curHasReading && !m_curIsTune) {
        m_lastValid = true;
        m_lastPeak  = m_curPeak;
        m_lastEndMs = nowMs;
    }
    m_curHasReading = false;
    m_curPeak       = 0.0;
    m_curIsTune     = false;
}

std::optional<double> TxPowerTracker::recentPeakWatts(qint64 nowMs, qint64 windowMs) const
{
    // Still keyed: the transmission in progress is the most recent one.
    if (m_transmitting && m_curHasReading && !m_curIsTune)
        return m_curPeak;
    if (m_lastValid && nowMs - m_lastEndMs <= windowMs)
        return m_lastPeak;
    return std::nullopt;
}

void TxPowerTracker::reset()
{
    *this = TxPowerTracker{};
}

double TxPowerTracker::roundForLog(double watts)
{
    if (!(watts > 0.0)) return 0.0;
    if (watts < 10.0) return std::round(watts * 10.0) / 10.0;
    return std::round(watts);
}

} // namespace ShackBook
