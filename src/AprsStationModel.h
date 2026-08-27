#pragma once

// AprsStationModel — a live roster of APRS stations heard off-air via
// AetherSDR's KISS TNC. One row per source call-SSID, deduped; each row
// carries the most-recent position/comment plus a packet count. Stations
// that haven't been heard for `staleAfterSecs()` drop out of the view.
//
// Distance/bearing are computed great-circle from the operator's grid
// (MY_GRIDSQUARE), so the table doubles as a "who's near me right now"
// radar for the 2 m / 70 cm transverter crew.
//
// Feed it decoded reports via addReport() (wire AprsKissClient::aprsReport
// straight in). It never touches a socket itself — pure model, testable.

#include "AprsDecode.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QHash>
#include <QVector>

namespace ShackBook {

class AprsStationModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        Call = 0,
        Distance,      // km from my grid ("" if either position unknown)
        Bearing,       // degrees true
        Symbol,        // APRS symbol table+code, e.g. "/>"
        Heard,         // "12s", "4m", "1h" ago
        Comment,       // status / comment text
        Worked,        // "✓" if this call is in the log
        ColumnCount
    };

    explicit AprsStationModel(QObject* parent = nullptr);

    // QAbstractTableModel
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orient,
                        int role = Qt::DisplayRole) const override;

    // Origin for distance/bearing. Pass MY_GRIDSQUARE (Maidenhead). An empty
    // or malformed grid disables distance/bearing (they render blank).
    void setMyGrid(const QString& grid);

    // Callsigns already in the log — shown with a ✓ in the Worked column.
    // Pass the base call OR call-SSID; matching is on the base call.
    void setWorkedCalls(const QSet<QString>& baseCalls);

    // How long a station stays in the roster after its last packet.
    int  staleAfterSecs() const { return m_staleAfterSecs; }
    void setStaleAfterSecs(int secs);

    // The station on a given row (for the map / detail panel). Row must be valid.
    struct Station {
        QString source;          // call-SSID as heard
        QDateTime firstHeard;
        QDateTime lastHeard;
        bool   hasPosition{false};
        double latitude{0.0};
        double longitude{0.0};
        char   symbolTable{'/'};
        char   symbolCode{'>'};
        QString comment;
        int    packets{0};
    };
    const Station& stationAt(int row) const { return m_stations.at(row); }

public slots:
    // Fold a freshly decoded report into the roster. `heardAt` defaults to the
    // caller's clock; pass an explicit time for testing.
    void addReport(const ShackBook::Aprs::Report& report,
                   const QDateTime& heardAt = QDateTime());

    // Drop stations older than staleAfterSecs(). Call on a timer (e.g. 10 s).
    void pruneStale(const QDateTime& now = QDateTime());

    // Repaint the "Heard" column so the relative-time strings ("12s"/"4m")
    // stay current even for rows that haven't received a new packet.
    void touchHeard();

signals:
    // Emitted whenever the roster changes (add/update/prune) so a map view
    // can re-plot. Carries the current station count for convenience.
    void rosterChanged(int stationCount);

private:
    int  indexOfSource(const QString& source) const;   // -1 if absent
    bool isWorked(const QString& source) const;

    QVector<Station>      m_stations;      // row order == insertion/most-recent
    QHash<QString, int>   m_indexBySource; // source -> row
    QSet<QString>         m_workedBase;    // base calls in the log (upper-case)
    QString               m_myGrid;
    bool                  m_haveOrigin{false};
    double                m_originLat{0.0};
    double                m_originLon{0.0};
    int                   m_staleAfterSecs{3600}; // default: last hour
};

} // namespace ShackBook
