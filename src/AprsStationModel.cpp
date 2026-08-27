#include "AprsStationModel.h"

#include <QColor>
#include <QRegularExpression>

#include <cmath>

namespace ShackBook {

namespace {

// Maidenhead locator -> lat/lon (centre of the square). Handles 4- or 6-char
// grids (e.g. "FM19" or "FM19ka"). Returns false on malformed input.
bool gridToLatLon(const QString& gridIn, double* lat, double* lon)
{
    const QString g = gridIn.trimmed().toUpper();
    static const QRegularExpression re(
        QStringLiteral("^[A-R]{2}[0-9]{2}([A-X]{2})?$"));
    if (!re.match(g).hasMatch())
        return false;

    double lo = (g.at(0).toLatin1() - 'A') * 20.0 - 180.0;
    double la = (g.at(1).toLatin1() - 'A') * 10.0 - 90.0;
    lo += (g.at(2).toLatin1() - '0') * 2.0;
    la += (g.at(3).toLatin1() - '0') * 1.0;

    if (g.size() >= 6) {
        lo += (g.at(4).toLatin1() - 'A') * (2.0 / 24.0);
        la += (g.at(5).toLatin1() - 'A') * (1.0 / 24.0);
        lo += (2.0 / 24.0) / 2.0;   // centre of the sub-square
        la += (1.0 / 24.0) / 2.0;
    } else {
        lo += 1.0;                  // centre of the 2°x1° square
        la += 0.5;
    }
    *lat = la;
    *lon = lo;
    return true;
}

double deg2rad(double d) { return d * M_PI / 180.0; }
double rad2deg(double r) { return r * 180.0 / M_PI; }

// Great-circle distance (km) and initial bearing (deg true), haversine.
void distanceBearing(double lat1, double lon1, double lat2, double lon2,
                     double* km, double* bearing)
{
    constexpr double R = 6371.0088; // mean Earth radius, km
    const double p1 = deg2rad(lat1), p2 = deg2rad(lat2);
    const double dp = deg2rad(lat2 - lat1), dl = deg2rad(lon2 - lon1);
    const double a = std::sin(dp / 2) * std::sin(dp / 2)
                   + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    *km = 2.0 * R * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2)
                   - std::sin(p1) * std::cos(p2) * std::cos(dl);
    double b = rad2deg(std::atan2(y, x));
    *bearing = std::fmod(b + 360.0, 360.0);
}

// Base call from a call-SSID ("G0JKN-9" -> "G0JKN").
QString baseCall(const QString& source)
{
    const int dash = source.indexOf('-');
    return (dash < 0 ? source : source.left(dash)).toUpper();
}

QString ago(const QDateTime& then, const QDateTime& now)
{
    const qint64 s = then.secsTo(now);
    if (s < 0)     return QStringLiteral("now");
    if (s < 60)    return QStringLiteral("%1s").arg(s);
    if (s < 3600)  return QStringLiteral("%1m").arg(s / 60);
    return QStringLiteral("%1h").arg(s / 3600);
}

} // namespace

AprsStationModel::AprsStationModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int AprsStationModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_stations.size();
}

int AprsStationModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant AprsStationModel::headerData(int section, Qt::Orientation orient, int role) const
{
    if (role != Qt::DisplayRole || orient != Qt::Horizontal)
        return {};
    switch (section) {
    case Call:     return QStringLiteral("Call");
    case Distance: return QStringLiteral("Dist (km)");
    case Bearing:  return QStringLiteral("Brg");
    case Symbol:   return QStringLiteral("Sym");
    case Heard:    return QStringLiteral("Heard");
    case Comment:  return QStringLiteral("Comment");
    case Worked:   return QStringLiteral("Log");
    default:       return {};
    }
}

QVariant AprsStationModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_stations.size())
        return {};
    const Station& st = m_stations.at(index.row());

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case Distance:
        case Bearing:
        case Heard:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        case Symbol:
        case Worked:
            return int(Qt::AlignCenter);
        default:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role == Qt::ForegroundRole && index.column() == Worked && isWorked(st.source))
        return QColor(0x1d, 0xb9, 0x54); // green ✓, matches the section-map theme

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case Call:
        return st.source;
    case Distance:
        if (m_haveOrigin && st.hasPosition) {
            double km, brg;
            distanceBearing(m_originLat, m_originLon, st.latitude, st.longitude, &km, &brg);
            return QString::number(km, 'f', km < 100 ? 1 : 0);
        }
        return {};
    case Bearing:
        if (m_haveOrigin && st.hasPosition) {
            double km, brg;
            distanceBearing(m_originLat, m_originLon, st.latitude, st.longitude, &km, &brg);
            return QStringLiteral("%1°").arg(qRound(brg));
        }
        return {};
    case Symbol:
        return QString(QChar(st.symbolTable)) + QChar(st.symbolCode);
    case Heard:
        return ago(st.lastHeard, QDateTime::currentDateTimeUtc());
    case Comment:
        return st.comment;
    case Worked:
        return isWorked(st.source) ? QStringLiteral("✓") : QString();
    default:
        return {};
    }
}

void AprsStationModel::setMyGrid(const QString& grid)
{
    m_myGrid = grid;
    m_haveOrigin = gridToLatLon(grid, &m_originLat, &m_originLon);
    if (!m_stations.isEmpty())
        emit dataChanged(index(0, Distance), index(m_stations.size() - 1, Bearing));
}

void AprsStationModel::setWorkedCalls(const QSet<QString>& baseCalls)
{
    m_workedBase.clear();
    for (const QString& c : baseCalls)
        m_workedBase.insert(baseCall(c));
    if (!m_stations.isEmpty())
        emit dataChanged(index(0, Worked), index(m_stations.size() - 1, Worked));
}

void AprsStationModel::setStaleAfterSecs(int secs)
{
    m_staleAfterSecs = qMax(1, secs);
}

int AprsStationModel::indexOfSource(const QString& source) const
{
    return m_indexBySource.value(source, -1);
}

bool AprsStationModel::isWorked(const QString& source) const
{
    return m_workedBase.contains(baseCall(source));
}

void AprsStationModel::addReport(const Aprs::Report& report, const QDateTime& heardAt)
{
    if (!report.isValid())
        return;
    const QDateTime now = heardAt.isValid() ? heardAt : QDateTime::currentDateTimeUtc();
    const QString src = report.source;

    const int row = indexOfSource(src);
    if (row >= 0) {
        Station& st = m_stations[row];
        st.lastHeard = now;
        ++st.packets;
        if (report.hasPosition) {
            st.hasPosition = true;
            st.latitude = report.latitude;
            st.longitude = report.longitude;
            st.symbolTable = report.symbolTable;
            st.symbolCode = report.symbolCode;
        }
        if (!report.comment.isEmpty())
            st.comment = report.comment;
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        emit rosterChanged(m_stations.size());
        return;
    }

    Station st;
    st.source = src;
    st.firstHeard = now;
    st.lastHeard = now;
    st.packets = 1;
    st.hasPosition = report.hasPosition;
    st.latitude = report.latitude;
    st.longitude = report.longitude;
    st.symbolTable = report.symbolTable;
    st.symbolCode = report.symbolCode;
    st.comment = report.comment;

    const int newRow = m_stations.size();
    beginInsertRows({}, newRow, newRow);
    m_stations.append(st);
    m_indexBySource.insert(src, newRow);
    endInsertRows();
    emit rosterChanged(m_stations.size());
}

void AprsStationModel::pruneStale(const QDateTime& nowIn)
{
    const QDateTime now = nowIn.isValid() ? nowIn : QDateTime::currentDateTimeUtc();
    bool removedAny = false;

    // Walk backwards so row indices stay valid as we remove.
    for (int i = m_stations.size() - 1; i >= 0; --i) {
        if (m_stations.at(i).lastHeard.secsTo(now) > m_staleAfterSecs) {
            beginRemoveRows({}, i, i);
            m_stations.remove(i);
            endRemoveRows();
            removedAny = true;
        }
    }
    if (removedAny) {
        // Row indices shifted — rebuild the source->row map.
        m_indexBySource.clear();
        for (int i = 0; i < m_stations.size(); ++i)
            m_indexBySource.insert(m_stations.at(i).source, i);
        emit rosterChanged(m_stations.size());
    }
}

void AprsStationModel::touchHeard()
{
    if (!m_stations.isEmpty())
        emit dataChanged(index(0, Heard), index(m_stations.size() - 1, Heard));
}

} // namespace ShackBook
