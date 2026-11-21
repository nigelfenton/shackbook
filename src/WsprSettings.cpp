#include "WsprSettings.h"

#include <algorithm>

namespace ShackBook {

namespace {
const QString kEnable  = QStringLiteral("WSPR_ENABLE");
const QString kHost    = QStringLiteral("WSPR_HOST");
const QString kPort    = QStringLiteral("WSPR_PORT");
const QString kPollSec = QStringLiteral("WSPR_POLL_SEC");

int validPort(int port)
{
    return (port >= 1 && port <= 65535) ? port : WsprConfig::kDefaultPort;
}

int validPollSec(int sec)
{
    return std::clamp(sec, WsprConfig::kMinPollSec, WsprConfig::kMaxPollSec);
}
} // namespace

WsprConfig loadWsprConfig(const WsprSettingGetter& get)
{
    WsprConfig cfg;
    cfg.enabled = get(kEnable, QStringLiteral("0")) == QLatin1String("1");
    cfg.host    = get(kHost, QString()).trimmed();
    cfg.port    = validPort(get(kPort, QString::number(WsprConfig::kDefaultPort)).toInt());
    bool ok = false;
    const int sec = get(kPollSec, QString::number(WsprConfig::kDefaultPollSec)).toInt(&ok);
    cfg.pollSec = validPollSec(ok ? sec : WsprConfig::kDefaultPollSec);
    return cfg;
}

void saveWsprConfig(const WsprConfig& cfg, const WsprSettingGetter& get,
                    const WsprSettingSetter& set)
{
    const WsprConfig now = loadWsprConfig(get);
    if (cfg.enabled != now.enabled)
        set(kEnable, cfg.enabled ? QStringLiteral("1") : QStringLiteral("0"));
    const QString host = cfg.host.trimmed();
    if (host != now.host)
        set(kHost, host);
    const int port = validPort(cfg.port);
    if (port != now.port)
        set(kPort, QString::number(port));
    const int sec = validPollSec(cfg.pollSec);
    if (sec != now.pollSec)
        set(kPollSec, QString::number(sec));
}

} // namespace ShackBook
