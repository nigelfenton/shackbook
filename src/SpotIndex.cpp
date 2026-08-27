#include "SpotIndex.h"

#include <QDateTime>
#include <cmath>

namespace ShackBook {

SpotIndex::SpotIndex(QObject* parent) : QObject(parent) {}

int SpotIndex::stepHzForMode(const QString& adifMode)
{
    const QString m = adifMode.toUpper();
    if (m == "CW" || m == "RTTY")            return 100;
    if (m == "FT8" || m == "FT4" || m == "PSK" ||
        m == "JT65" || m == "JT9" || m == "JS8" ||
        m == "MFSK")                          return 100;
    // SSB / AM / FM / USB / LSB / phone-anything → 1 kHz channelised step.
    // Covers the typical phone wiggle of a few hundred Hz.
    return 1000;
}

qint64 SpotIndex::roundedHz(double freqMhz, int stepHz)
{
    if (stepHz <= 0) stepHz = 1;
    const qint64 hz = static_cast<qint64>(std::llround(freqMhz * 1.0e6));
    // Round to nearest step (not floor), so 14250500 with 1 kHz step
    // lands on 14251000, not 14250000.
    return ((hz + stepHz / 2) / stepHz) * stepHz;
}

qint64 SpotIndex::bucketKey(double freqMhz, const QString& adifMode)
{
    const int step = stepHzForMode(adifMode);
    const qint64 hz = roundedHz(freqMhz, step);
    // Mix a small mode-tag into the upper bits so different modes at the
    // same step never collide.  Sidebands are intentionally collapsed
    // into the same tag — POTA spots come in as "USB" / "LSB" / "SSB"
    // depending on the spotter, but they're all the same QSO mode.
    qint64 modeTag = 0;
    const QString m = adifMode.toUpper();
    if      (m == "SSB" || m == "USB" || m == "LSB")  modeTag = 1;
    else if (m == "CW"  || m == "CWL" || m == "CWU")  modeTag = 2;
    else if (m == "AM"  || m == "SAM")                modeTag = 3;
    else if (m == "FM"  || m == "NFM" || m == "DFM")  modeTag = 4;
    else if (m == "RTTY")                             modeTag = 5;
    else if (m == "FT8" || m == "FT4" || m == "PSK"  ||
             m == "JS8" || m == "MFSK"|| m == "DATA" ||
             m == "DIGITAL"|| m == "DIGITALVOICE")    modeTag = 6;
    return hz | (modeTag << 40);
}

void SpotIndex::addOrUpdate(const SpotData& spot)
{
    if (spot.call.isEmpty()) return;
    SpotData s = spot;
    if (!s.receivedAt.isValid()) s.receivedAt = QDateTime::currentDateTimeUtc();

    // If an older entry for this call exists, drop its bucket pointer
    // (its freq may differ from the new one).
    if (auto old = m_byCall.constFind(s.call); old != m_byCall.constEnd()) {
        const qint64 oldKey = bucketKey(old->freqMhz, old->mode);
        if (m_byBucket.value(oldKey) == s.call) m_byBucket.remove(oldKey);
    }

    m_byCall.insert(s.call, s);
    m_byBucket.insert(bucketKey(s.freqMhz, s.mode), s.call);
    emit spotAdded(s);
}

int SpotIndex::purgeExpired()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    int removed = 0;
    for (auto it = m_byCall.begin(); it != m_byCall.end(); ) {
        const qint64 ageSec = it->receivedAt.secsTo(now);
        if (ageSec > it->lifetimeSec) {
            const qint64 key = bucketKey(it->freqMhz, it->mode);
            if (m_byBucket.value(key) == it->call) m_byBucket.remove(key);
            it = m_byCall.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) emit spotsRemoved(removed);
    return removed;
}

std::optional<SpotData> SpotIndex::findAt(double freqMhz, const QString& adifMode) const
{
    if (freqMhz <= 0.0) return std::nullopt;

    const int step = stepHzForMode(adifMode);
    const qint64 hz = roundedHz(freqMhz, step);

    auto lookupBucket = [&](qint64 hzBucket, bool requireBlankMode) -> std::optional<SpotData> {
        for (auto k = m_byBucket.constBegin(); k != m_byBucket.constEnd(); ++k) {
            if ((k.key() & 0xFFFFFFFFFFLL) != hzBucket) continue;
            auto cIt = m_byCall.constFind(k.value());
            if (cIt == m_byCall.constEnd()) continue;
            if (requireBlankMode && !cIt->mode.isEmpty()) continue;
            return *cIt;
        }
        return std::nullopt;
    };

    // 1. Exact bucket + exact mode (the normal hit).
    const qint64 key = bucketKey(freqMhz, adifMode);
    auto it = m_byBucket.constFind(key);
    if (it != m_byBucket.constEnd()) {
        auto cIt = m_byCall.constFind(*it);
        if (cIt != m_byCall.constEnd()) return *cIt;
    }

    // 2. Adjacent bucket (one step above / below) at this freq, exact
    //    mode.  Catches the boundary case where the spotter and the
    //    operator round to different sides of the bucket edge — e.g.
    //    spot at 7158.6 (bucket 7159), operator at 7158.0 (bucket 7158).
    for (qint64 dh : { -static_cast<qint64>(step), +static_cast<qint64>(step) }) {
        const qint64 adjKey = (hz + dh) | (key & ~0xFFFFFFFFFFLL);
        auto k2 = m_byBucket.constFind(adjKey);
        if (k2 != m_byBucket.constEnd()) {
            auto cIt = m_byCall.constFind(*k2);
            if (cIt != m_byCall.constEnd()) return *cIt;
        }
    }

    // 3. Same Hz bucket but the spot was filed with no mode (some DX
    //    cluster nodes don't include mode in the wire format).  Fall
    //    through to a mode-agnostic match at the exact bucket only.
    if (auto res = lookupBucket(hz, /*requireBlankMode=*/true)) return res;

    return std::nullopt;
}

QVector<SpotData> SpotIndex::snapshot() const
{
    QVector<SpotData> out;
    out.reserve(m_byCall.size());
    for (auto it = m_byCall.constBegin(); it != m_byCall.constEnd(); ++it) {
        out.append(it.value());
    }
    std::sort(out.begin(), out.end(),
              [](const SpotData& a, const SpotData& b) {
                  return a.freqMhz < b.freqMhz;
              });
    return out;
}

} // namespace ShackBook
