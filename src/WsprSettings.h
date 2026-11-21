#pragma once

// WsprSettings — where ShackBook finds an AetherWSPR receiver, per log (#42).
//
// The WSPR spot feed (WsprClient) has read these keys since #42, but nothing
// in the app wrote them, so the feed could not be switched on. Settings ->
// DX Cluster now does, and MainWindow and SettingsDialog both go through
// these two functions, so the keys, defaults and limits live in one place.
// Plain get/set functions rather than LogbookModel, so they test without a
// database (the CwKeyerSettings pattern).
//
// Keys:
//   WSPR_ENABLE      "1" / "0"; ABSENT MEANS OFF
//   WSPR_HOST        the radio's address, where the agent runs; trimmed
//   WSPR_PORT        the agent's HTTP port; 48995 unless set, and a value
//                    outside 1-65535 loads as 48995
//   WSPR_POLL_SEC    seconds between polls; 60 unless set, clamped to
//                    10-3600 (WSPR cycles are two minutes, so faster
//                    gains nothing)

#include <QString>

#include <functional>

namespace ShackBook {

struct WsprConfig {
    static constexpr int kDefaultPort    = 48995;
    static constexpr int kDefaultPollSec = 60;
    static constexpr int kMinPollSec     = 10;
    static constexpr int kMaxPollSec     = 3600;

    bool    enabled = false;
    QString host;
    int     port    = kDefaultPort;
    int     pollSec = kDefaultPollSec;

    // The feed only runs when it is on AND knows where to look.
    bool runnable() const { return enabled && !host.isEmpty(); }
};

using WsprSettingGetter = std::function<QString(const QString& key, const QString& def)>;
using WsprSettingSetter = std::function<void(const QString& key, const QString& value)>;

WsprConfig loadWsprConfig(const WsprSettingGetter& get);
// Writes only the keys whose value differs from what `get` loads now, so an
// OK in Settings with nothing changed writes nothing.
void       saveWsprConfig(const WsprConfig& cfg, const WsprSettingGetter& get,
                          const WsprSettingSetter& set);

} // namespace ShackBook
