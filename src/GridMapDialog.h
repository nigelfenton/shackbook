#pragma once

// GridMapDialog — Maidenhead grid tracker (Maps → Grid Map).
//
// A true-geography sibling of the Section Map cartogram: equirectangular
// world, the 18×18 field lattice labelled AA–RR, and every 4-character
// square worked in the log painted at its real 2°×1° cell — dim green for
// worked, bright for confirmed (LoTW or QSL card, the same rule the Awards
// panel uses). Band and mode filters make it a per-band VUCC-style chase
// view; the map repaints live as QSOs land. Hovering a cell names the
// square and the first/latest call worked there.
//
// The continent backdrop is deliberately coarse hand-laid geometry (same
// philosophy as the section cartogram's "roughly geographic" tiles): it
// exists to orient the eye, not to be a chart.

#include <QDialog>
#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;

namespace ShackBook {

class LogbookModel;

struct GridCellStat {
    int     count{0};
    bool    confirmed{false};
    QString firstCall;
    QString lastCall;
};

// The painted map itself. Kept widget-only (no model knowledge) so it can
// be reused elsewhere later; the dialog feeds it a grid → stat hash.
class GridMapWidget : public QWidget {
    Q_OBJECT

public:
    explicit GridMapWidget(QWidget* parent = nullptr);

    void setCells(const QHash<QString, GridCellStat>& cells);

signals:
    // A WORKED square was clicked (select), or the selection was cleared
    // (empty string) by clicking it again / clicking anywhere else.
    void squareClicked(const QString& square);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;

private:
    QRectF cellRect(const QString& square) const;   // 4-char square → widget rect
    QString squareAt(const QPoint& pos) const;      // widget point → 4-char square

    QHash<QString, GridCellStat> m_cells;
    QString m_selected;
};

class GridMapDialog : public QDialog {
    Q_OBJECT

public:
    explicit GridMapDialog(LogbookModel* model, QWidget* parent = nullptr);

signals:
    // Forwarded square selection for the main window's filter row.
    void filterRequested(const QString& square);

public slots:
    void refresh();

private:
    LogbookModel* m_model{};
    GridMapWidget* m_map{};
    QComboBox* m_bandFilter{};
    QComboBox* m_modeFilter{};
    QLabel* m_countLabel{};
    QLabel* m_statsLabel{};
};

} // namespace ShackBook
