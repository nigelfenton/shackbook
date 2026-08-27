#pragma once

// SpotIndex — in-memory store of recently-seen spots, queryable by
// frequency + mode for the auto-fill feature.
//
// Spots are bucketed by (mode-aware step-rounded freq, mode), which
// matches the convention that operators "wiggle" around the spot's
// nominal freq:
//   • SSB / AM / FM  → 1 kHz step (so 14250.4 LSB lives at bucket 14250 LSB)
//   • CW / RTTY      → 100 Hz step
//   • FT8 / FT4 / data → 100 Hz step
// findAt() rounds the radio's freq to the same step before lookup, so a
// radio on 14250400 LSB pulls the spot for 14250 LSB.
//
// Expiry: each entry carries lifetimeSec (default 30 min); purgeExpired()
// drops anything past that.  Call it periodically (e.g. once per minute)
// from a QTimer in the owning code.

#include "SpotData.h"

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <optional>

namespace ShackBook {

class SpotIndex : public QObject {
    Q_OBJECT

public:
    explicit SpotIndex(QObject* parent = nullptr);

    // Insert a spot, replacing any existing spot for the same call (calls
    // re-spotted at a different freq invalidate the old entry).
    void addOrUpdate(const SpotData& spot);

    // Remove anything whose age exceeds lifetimeSec.  Returns the number
    // of entries removed.
    int  purgeExpired();

    // Find a spot matching the given radio freq + mode.  Returns nullopt
    // if no current spot is bucketed at that step.  When multiple spots
    // hash to the same bucket (rare), the most recent one wins.
    std::optional<SpotData> findAt(double freqMhz, const QString& adifMode) const;

    int size() const { return m_byCall.size(); }

    // Step in Hz used for the given mode — exposed so callers can use
    // the same rounding rule outside the index (e.g. UI display).
    static int stepHzForMode(const QString& adifMode);

    // Snapshot of all current spots — used by the diagnostic Tools →
    // Show Spot Index dialog.  Sorted by freq ascending.
    QVector<SpotData> snapshot() const;

signals:
    void spotAdded(const SpotData& spot);
    void spotsRemoved(int n);

private:
    static qint64  bucketKey(double freqMhz, const QString& adifMode);
    static qint64  roundedHz(double freqMhz, int stepHz);

    // Primary keyed by callsign so a re-spot updates in place.
    QHash<QString, SpotData>  m_byCall;
    // Secondary index: bucket key → most-recent call in that bucket.
    QHash<qint64, QString>    m_byBucket;
};

} // namespace ShackBook
