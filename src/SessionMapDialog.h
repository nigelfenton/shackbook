#pragma once

// SessionMapDialog — where did tonight's QSOs go? (Maps → Session Map, #3)
//
// The Grid Map answers "what have I ever worked". After an evening at the
// radio the more interesting question is where the signal actually got out
// TONIGHT: which bands were open, which direction was working, whether that
// new antenna or amp changed the footprint.
//
// Draws the same world as the Grid Map (shared geometry in WorldMap.h) but
// plots the session's contacts as points with a great-circle line from the
// station's own grid, coloured by band so a multi-band evening reads as
// separate footprints rather than one scatter.
//
// A session is DERIVED — see QsoSession.h for the >2 hour rule. Previous /
// Next walk back through earlier evenings.
//
// Honesty about coverage, the same as the Grid Map's `scanned` count: QSOs
// without a usable grid square cannot be placed, and the dialog SAYS how
// many rather than quietly dropping them. In a real log that is a large
// fraction — hand-logged SSB contacts often carry no grid at all.

#include "QsoSession.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QPushButton;

namespace ShackBook {

class LogbookModel;

// One plotted contact, already projected to lat/lon.
struct SessionPoint {
    double  lat{0};
    double  lon{0};
    QString call;
    QString band;
    QString grid;
};

// The painted map. Widget-only (no model knowledge), matching GridMapWidget's
// split so it can be reused elsewhere.
class SessionMapWidget : public QWidget {
    Q_OBJECT

public:
    explicit SessionMapWidget(QWidget* parent = nullptr);

    // `haveOrigin` false draws points without great-circle lines, which is
    // what happens when MY_GRIDSQUARE is unset — the map still works.
    void setSession(const QVector<SessionPoint>& points,
                    double originLat, double originLon, bool haveOrigin);

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    QVector<SessionPoint> m_points;
    double m_originLat{0};
    double m_originLon{0};
    bool   m_haveOrigin{false};
};

class SessionMapDialog : public QDialog {
    Q_OBJECT

public:
    explicit SessionMapDialog(LogbookModel* model, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    void showSession(int index);

    LogbookModel*        m_model{};
    SessionMapWidget*    m_map{};
    QLabel*              m_titleLabel{};
    QLabel*              m_statsLabel{};
    QLabel*              m_bandsLabel{};
    QPushButton*         m_prevBtn{};
    QPushButton*         m_nextBtn{};

    QVector<QsoSession>  m_sessions;
    int                  m_current{0};
    int                  m_unplaceableTime{0};
};

} // namespace ShackBook
