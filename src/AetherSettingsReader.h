#pragma once

// AetherSettingsReader — read AetherSDR's persisted settings file to
// discover which DX cluster the operator is logged into.
//
// AetherSDR stores settings as XML at:
//   <ConfigLocation>/AetherSDR/AetherSDR.settings
// (Windows: %APPDATA%\AetherSDR\AetherSDR.settings)
//
// We only care about a small handful of keys for the spot-autofill
// feature.  This class is a one-shot reader — call read() once at
// startup; if the file moves around, the user can also re-trigger from
// the Settings dialog.
//
// We deliberately do NOT write back to AetherSDR's file.  This is a
// read-only peek, by design, so AetherSDR remains a black box.

#include <QString>

namespace ShackBook {

struct AetherDxClusterConfig {
    bool    found{false};      // true if at least Host/Port were read
    QString host;              // e.g. "dxc.nc7j.com"
    int     port{0};
    QString callsign;          // operator's primary cluster login
    bool    autoConnect{false};
    QString sourcePath;        // file we read from, for diagnostics
};

class AetherSettingsReader {
public:
    // Default file path (per AetherSDR's AppSettings convention).  Empty
    // string == platform default.
    static QString defaultSettingsPath();

    // Read the settings file at `path` (or defaultSettingsPath() if empty)
    // and return DX cluster fields.  found == false if the file doesn't
    // exist or doesn't contain DxCluster* keys.
    static AetherDxClusterConfig readDxClusterConfig(const QString& path = {});
};

} // namespace ShackBook
