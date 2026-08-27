#include "AetherSettingsReader.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace ShackBook {

QString AetherSettingsReader::defaultSettingsPath()
{
    // AetherSDR uses QStandardPaths::ConfigLocation, which on Windows
    // resolves to %LOCALAPPDATA%\<OrgName>\<AppName>.  Calling it from
    // ShackBook gives ShackBook's own org/app subdirectory, NOT
    // AetherSDR's — so we have to construct the path against the
    // *unscoped* config base.  Use the platform's well-known per-user
    // config root and append AetherSDR's path inside it directly.
    QString base;
#ifdef Q_OS_WIN
    base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
#elif defined(Q_OS_MACOS)
    base = QDir::homePath() + "/Library/Application Support";
#else  // Linux / other UNIX
    base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty()) base = QDir::homePath() + "/.config";
#endif
    return base + "/AetherSDR/AetherSDR.settings";
}

AetherDxClusterConfig AetherSettingsReader::readDxClusterConfig(const QString& pathIn)
{
    AetherDxClusterConfig out;
    out.sourcePath = pathIn.isEmpty() ? defaultSettingsPath() : pathIn;

    QFile f(out.sourcePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;

    // The schema is flat at the top level for non-station-specific keys:
    //   <Settings>
    //     <DxClusterHost>dxc.nc7j.com</DxClusterHost>
    //     <DxClusterPort>7300</DxClusterPort>
    //     <DxClusterCallsign>G0JKN</DxClusterCallsign>
    //     <DxClusterAutoConnect>True</DxClusterAutoConnect>
    //     <StationName>home</StationName>
    //     <home>            <!-- station-specific section -->
    //       <DxClusterHost>...</DxClusterHost>   <!-- if present, wins -->
    //     </home>
    //   </Settings>
    //
    // We collect both top-level and station-specific values; if both are
    // present the station-specific entry overrides.
    QXmlStreamReader xml(&f);
    QString stationName;
    QString currentStation;     // non-empty when we're inside a station section

    QString hostTop, hostStation;
    QString portTop, portStation;
    QString callTop, callStation;
    QString autoTop, autoStation;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString tag = xml.name().toString();
            if (tag == "Settings") continue;

            if (tag == stationName && !stationName.isEmpty() && currentStation.isEmpty()) {
                currentStation = tag;
                continue;
            }

            const QString text = xml.readElementText();
            const bool inStation = !currentStation.isEmpty();

            if      (tag == "StationName")        stationName = text;
            else if (tag == "DxClusterHost")      (inStation ? hostStation : hostTop)  = text;
            else if (tag == "DxClusterPort")      (inStation ? portStation : portTop)  = text;
            else if (tag == "DxClusterCallsign")  (inStation ? callStation : callTop)  = text;
            else if (tag == "DxClusterAutoConnect") (inStation ? autoStation : autoTop) = text;
        } else if (xml.isEndElement()) {
            if (xml.name().toString() == stationName && !currentStation.isEmpty())
                currentStation.clear();
        }
    }
    f.close();

    auto pick = [](const QString& station, const QString& top) {
        return !station.isEmpty() ? station : top;
    };
    out.host        = pick(hostStation, hostTop);
    out.port        = pick(portStation, portTop).toInt();
    out.callsign    = pick(callStation, callTop);
    out.autoConnect = pick(autoStation, autoTop).compare("True", Qt::CaseInsensitive) == 0;
    out.found       = !out.host.isEmpty() && out.port > 0;
    return out;
}

} // namespace ShackBook
