#pragma once

// SpotData — one DX/POTA/cluster spot, normalised for use by SpotIndex.
//
// Sources are heterogeneous (telnet DX cluster, POTA HTTP API, future
// RBN feed) so this struct is the lowest common denominator.  Mode and
// comment are best-effort: cluster spots usually have a comment but
// rarely a mode, POTA spots have a mode but no comment, etc.

#include <QString>
#include <QDateTime>

namespace ShackBook {

struct SpotData {
    QString call;                         // DX station's callsign
    double  freqMhz{0.0};                 // operator's transmit frequency
    QString mode;                         // optional ADIF base mode (SSB/CW/FT8/...)
    QString comment;                      // free-form (cluster note, park ref, ...)
    QString source;                       // human-readable origin: "DXC: dxc.nc7j.com", "POTA"
    QDateTime receivedAt;                 // when this spot first arrived
    int     lifetimeSec{1800};            // discard after this many seconds (default 30 min)
    // Contest state from a logger's bandmap (N1MM+/DXLog, #11): one of
    // "bust", "dupe", "mult", "cq", "busy", "qtc", "new", or empty.
    // Cluster and POTA spots leave it empty.
    QString status;
    QString spotter;                      // who spotted it, when the source says
};

} // namespace ShackBook
