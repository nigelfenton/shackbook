#include "GridMapDialog.h"

#include "LogbookModel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <QToolTip>
#include <QVBoxLayout>

namespace ShackLog {

namespace {

// Palette — HF-dashboard dark, matching the Section Map's tones.
const QColor kSea(0x0a, 0x10, 0x1e);
const QColor kLand(0x14, 0x1e, 0x30);
const QColor kLandEdge(0x20, 0x30, 0x48);
const QColor kFieldLine(0x1c, 0x2a, 0x40);
const QColor kFieldLabel(0x3a, 0x4c, 0x66);
const QColor kWorked(0x0e, 0x7a, 0x33);
const QColor kConfirmed(0x1d, 0xb9, 0x54);

// Deliberately coarse continent outlines, (lon, lat) vertex lists.
// Hand-laid like the section cartogram — orientation, not navigation.
using Pt = QPointF;
const QVector<QVector<Pt>> kLand110 = {
    // North America
    {{-168,66},{-157,71},{-140,70},{-125,70},{-110,73},{-95,72},{-82,73},{-75,68},
     {-81,62},{-93,58},{-82,55},{-79,51},{-70,47},{-60,50},{-52,47},{-65,44},{-70,42},
     {-75,36},{-80,32},{-81,26},{-84,30},{-89,29},{-94,29},{-97,26},{-97,22},{-94,18},
     {-88,16},{-83,10},{-79,9},{-83,8},{-87,13},{-92,15},{-97,16},{-105,20},{-110,24},
     {-114,29},{-118,33},{-123,38},{-125,43},{-124,48},{-130,54},{-135,58},{-142,60},
     {-152,58},{-158,56},{-166,60},{-168,66}},
    // Greenland
    {{-46,60},{-53,66},{-55,70},{-60,74},{-68,77},{-58,80},{-45,83},{-32,83},{-22,80},
     {-20,75},{-22,70},{-30,68},{-40,64},{-46,60}},
    // South America
    {{-79,9},{-75,11},{-71,12},{-64,11},{-60,9},{-52,5},{-50,0},{-44,-3},{-38,-5},
     {-35,-9},{-39,-14},{-40,-20},{-48,-25},{-53,-33},{-57,-38},{-62,-41},{-65,-47},
     {-69,-52},{-71,-54},{-75,-50},{-73,-44},{-72,-37},{-70,-30},{-70,-22},{-75,-15},
     {-81,-6},{-80,0},{-77,6},{-79,9}},
    // Europe
    {{-10,36},{-9,43},{-2,44},{-5,48},{-2,50},{2,51},{4,53},{8,54},{8,57},{11,56},
     {13,55},{18,55},{21,56},{28,60},{31,63},{28,66},{25,71},{31,70},{40,68},{44,67},
     {40,65},{37,62},{31,60},{30,55},{35,47},{30,45},{28,41},{23,37},{22,40},{18,40},
     {15,38},{16,42},{12,44},{10,43},{8,44},{3,42},{0,40},{-2,37},{-6,36},{-10,36}},
    // Africa
    {{-6,36},{-2,35},{3,37},{10,37},{11,34},{19,32},{25,32},{32,31},{34,28},{37,21},
     {40,15},{43,12},{48,11},{51,12},{46,5},{41,-2},{39,-7},{36,-14},{35,-20},{33,-26},
     {28,-33},{20,-35},{17,-29},{14,-22},{12,-15},{9,-7},{9,0},{6,4},{-2,5},{-8,4},
     {-13,8},{-17,12},{-17,15},{-16,20},{-13,26},{-10,31},{-6,36}},
    // Asia (mainland, coarse)
    {{40,68},{44,67},{55,68},{68,69},{73,68},{80,72},{95,76},{105,77},{113,74},
     {130,71},{140,72},{160,70},{170,70},{178,66},{170,60},{162,58},{157,52},{142,54},
     {135,44},{129,42},{127,40},{122,39},{122,31},{116,23},{108,20},{105,10},{103,2},
     {98,8},{97,17},{91,22},{88,22},{80,15},{77,8},{73,20},{68,24},{66,25},{57,26},
     {52,26},{48,30},{44,38},{36,36},{27,37},{26,40},{30,41},{36,45},{47,42},{54,45},
     {50,50},{55,55},{60,55},{60,62},{50,62},{44,66},{40,68}},
    // Japan (blob)
    {{130,31},{132,34},{136,35},{140,36},{141,40},{142,43},{145,44},{143,42},{141,38},
     {140,35},{136,33},{131,30},{130,31}},
    // Indonesia / Malaya arc (blob)
    {{95,6},{100,2},{104,-2},{110,-7},{117,-8},{124,-9},{131,-8},{136,-6},{141,-7},
     {147,-9},{143,-4},{137,-2},{131,-1},{127,1},{120,1},{114,2},{109,3},{103,6},{95,6}},
    // Australia
    {{114,-22},{113,-25},{115,-33},{118,-35},{124,-33},{130,-32},{136,-35},{140,-38},
     {147,-39},{150,-37},{153,-32},{153,-27},{151,-24},{146,-19},{142,-13},{136,-12},
     {132,-11},{127,-14},{122,-17},{114,-22}},
    // New Zealand (blob)
    {{173,-35},{176,-38},{178,-38},{176,-41},{172,-44},{167,-46},{170,-44},{172,-40},
     {173,-35}},
    // Antarctica (band)
    {{-180,-64},{-120,-70},{-60,-64},{0,-68},{60,-66},{120,-65},{180,-64},{180,-90},
     {-180,-90},{-180,-64}},
    // UK + Ireland (blob) — this map's audience would notice its absence.
    {{-10,52},{-6,55},{-5,58},{-3,59},{-2,56},{0,53},{1,51},{-3,50},{-6,50},{-10,52}},
};

// The confirmed rule the Awards panel uses: LoTW or a QSL card.
bool isConfirmed(const Qso& q)
{
    return q.lotwRcvd.compare(QStringLiteral("Y"), Qt::CaseInsensitive) == 0
        || q.qslRcvd.compare(QStringLiteral("Y"), Qt::CaseInsensitive) == 0;
}

// Normalize a logged gridsquare to its 4-char Maidenhead square, or empty.
QString squareOf(const QString& grid)
{
    const QString g = grid.trimmed().left(4).toUpper();
    static const QRegularExpression re(
        QStringLiteral("^[A-R][A-R][0-9][0-9]$"));
    return re.match(g).hasMatch() ? g : QString();
}

} // namespace

// ── GridMapWidget ────────────────────────────────────────────────────────

GridMapWidget::GridMapWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(720, 360);
}

void GridMapWidget::setCells(const QHash<QString, GridCellStat>& cells)
{
    m_cells = cells;
    update();
}

QRectF GridMapWidget::cellRect(const QString& square) const
{
    const double lonW = width() / 360.0, latH = height() / 180.0;
    const double lon = (square.at(0).toLatin1() - 'A') * 20.0
                       + (square.at(2).toLatin1() - '0') * 2.0 - 180.0;
    const double lat = (square.at(1).toLatin1() - 'A') * 10.0
                       + (square.at(3).toLatin1() - '0') * 1.0 - 90.0;
    // y axis: +lat is up the map, down the widget; the cell spans
    // [lat, lat+1), so its top edge sits at 90 - (lat + 1).
    return {(lon + 180.0) * lonW, (89.0 - lat) * latH,
            2.0 * lonW, 1.0 * latH};
}

QString GridMapWidget::squareAt(const QPoint& pos) const
{
    const double lon = pos.x() * 360.0 / width() - 180.0;
    const double lat = 90.0 - pos.y() * 180.0 / height();
    if (lon < -180 || lon >= 180 || lat <= -90 || lat > 90) return {};
    // qMin: lat == 90.0 exactly (top edge) belongs to the R field, not a
    // nonexistent 19th one.
    const int fLon = qMin(17, static_cast<int>((lon + 180.0) / 20.0));
    const int fLat = qMin(17, static_cast<int>((lat + 90.0) / 10.0));
    const int sLon = qMin(9, static_cast<int>((lon + 180.0 - fLon * 20.0) / 2.0));
    const int sLat = qMin(9, static_cast<int>((lat + 90.0 - fLat * 10.0) / 1.0));
    return QString("%1%2%3%4")
        .arg(QChar('A' + fLon)).arg(QChar('A' + fLat))
        .arg(sLon).arg(sLat);
}

void GridMapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double lonW = width() / 360.0, latH = height() / 180.0;
    auto toXY = [&](const Pt& ll) {
        return QPointF((ll.x() + 180.0) * lonW, (90.0 - ll.y()) * latH);
    };

    p.fillRect(rect(), kSea);

    // Continents.
    p.setPen(QPen(kLandEdge, 1.0));
    p.setBrush(kLand);
    for (const auto& poly : kLand110) {
        QPainterPath path;
        path.moveTo(toXY(poly.first()));
        for (int i = 1; i < poly.size(); ++i) path.lineTo(toXY(poly[i]));
        path.closeSubpath();
        p.drawPath(path);
    }

    // Worked / confirmed squares.
    for (auto it = m_cells.constBegin(); it != m_cells.constEnd(); ++it) {
        const QRectF r = cellRect(it.key());
        QColor fill = it->confirmed ? kConfirmed : kWorked;
        fill.setAlpha(it->confirmed ? 230 : 190);
        p.fillRect(r, fill);
    }

    // Selection halo under the lattice so the border reads crisply.
    if (!m_selected.isEmpty() && m_cells.contains(m_selected)) {
        const QRectF r = cellRect(m_selected);
        p.setPen(QPen(QColor(0xff, 0xd1, 0x4d), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(-1, -1, 1, 1));
    }

    // Field lattice + labels on top.
    p.setPen(QPen(kFieldLine, 1.0));
    for (int i = 0; i <= 18; ++i) {
        const double x = i * 20.0 * lonW, y = i * 10.0 * latH;
        p.drawLine(QPointF(x, 0), QPointF(x, height()));
        p.drawLine(QPointF(0, y), QPointF(width(), y));
    }
    p.setPen(kFieldLabel);
    QFont f = font();
    f.setFamily(QStringLiteral("Consolas"));
    f.setPointSizeF(qMax(6.0, height() / 55.0));
    p.setFont(f);
    for (int fx = 0; fx < 18; ++fx)
        for (int fy = 0; fy < 18; ++fy) {
            const QString name = QString("%1%2").arg(QChar('A' + fx))
                                                .arg(QChar('A' + fy));
            const QRectF r(fx * 20.0 * lonW, (17 - fy) * 10.0 * latH,
                           20.0 * lonW, 10.0 * latH);
            p.drawText(r, Qt::AlignCenter, name);
        }
}

void GridMapWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;
    const QString sq = squareAt(ev->pos());
    // Only worked squares select — filtering the log to an empty square
    // would just blank the table. Clicking the selection (or anywhere
    // else) clears it.
    const QString next =
        (m_cells.contains(sq) && sq != m_selected) ? sq : QString();
    if (next == m_selected) return;
    m_selected = next;
    update();
    emit squareClicked(m_selected);
}

void GridMapWidget::mouseMoveEvent(QMouseEvent* ev)
{
    const QString sq = squareAt(ev->pos());
    if (sq.isEmpty()) { QToolTip::hideText(); return; }
    const auto it = m_cells.constFind(sq);
    QString tip = sq;
    if (it != m_cells.constEnd()) {
        tip += QString(" — %1 QSO(s)%2\nfirst: %3   latest: %4")
                   .arg(it->count)
                   .arg(it->confirmed ? QStringLiteral(", confirmed") : QString())
                   .arg(it->firstCall, it->lastCall);
    }
    QToolTip::showText(ev->globalPosition().toPoint(), tip, this);
}

// ── GridMapDialog ────────────────────────────────────────────────────────

GridMapDialog::GridMapDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle(QStringLiteral("Grid Map — Maidenhead squares"));
    setMinimumSize(980, 560);

    auto* main = new QVBoxLayout(this);
    main->setSpacing(8);

    auto* header = new QHBoxLayout;
    m_countLabel = new QLabel(QStringLiteral("0"));
    m_countLabel->setStyleSheet(
        "QLabel { color: #1db954; font-size: 22px; font-weight: bold; "
        "font-family: Consolas, 'Cascadia Mono', monospace; }");
    m_statsLabel = new QLabel;
    m_statsLabel->setStyleSheet("QLabel { color: #6b8099; font-size: 11px; }");
    header->addWidget(m_countLabel);
    header->addSpacing(16);
    header->addWidget(m_statsLabel);
    header->addStretch();
    header->addWidget(new QLabel(QStringLiteral("Band")));
    m_bandFilter = new QComboBox;
    header->addWidget(m_bandFilter);
    header->addSpacing(8);
    header->addWidget(new QLabel(QStringLiteral("Mode")));
    m_modeFilter = new QComboBox;
    header->addWidget(m_modeFilter);
    main->addLayout(header);

    m_map = new GridMapWidget(this);
    main->addWidget(m_map, /*stretch*/ 1);

    auto* foot = new QLabel(QStringLiteral(
        "Dim green = worked · bright green = confirmed (LoTW or QSL card, "
        "as the Awards panel counts it) · hover a square for details · "
        "click a worked square to filter the main log to it (click again "
        "to clear). Filters re-count live; new QSOs appear as logged."));
    foot->setStyleSheet("QLabel { color: #6b8099; font-size: 11px; }");
    foot->setWordWrap(true);
    main->addWidget(foot);

    // Filter combos are (re)filled inside refresh() from what the log holds.
    connect(m_bandFilter, &QComboBox::currentTextChanged,
            this, &GridMapDialog::refresh);
    connect(m_modeFilter, &QComboBox::currentTextChanged,
            this, &GridMapDialog::refresh);

    connect(m_map, &GridMapWidget::squareClicked,
            this, &GridMapDialog::filterRequested);

    connect(m_model, &LogbookModel::qsoAdded,   this, &GridMapDialog::refresh);
    connect(m_model, &LogbookModel::qsoUpdated, this, &GridMapDialog::refresh);
    connect(m_model, &LogbookModel::qsoDeleted, this, &GridMapDialog::refresh);

    refresh();
}

void GridMapDialog::refresh()
{
    if (!m_model || !m_model->isOpen()) return;

    const QString bandSel = m_bandFilter->currentText();
    const QString modeSel = m_modeFilter->currentText();

    QHash<QString, GridCellStat> cells;
    QSet<QString> bands, modes;
    int scanned = 0, noGrid = 0, confirmedSquares = 0;

    const auto qsos = m_model->queryQsos();      // whole log, newest first
    scanned = qsos.size();
    for (const auto& q : qsos) {
        if (!q.band.isEmpty()) bands.insert(q.band.toLower());
        if (!q.mode.isEmpty()) modes.insert(q.mode.toUpper());
        if (!bandSel.isEmpty() && bandSel != QStringLiteral("All")
            && q.band.toLower() != bandSel) continue;
        if (!modeSel.isEmpty() && modeSel != QStringLiteral("All")
            && q.mode.toUpper() != modeSel) continue;
        const QString sq = squareOf(q.gridsquare);
        if (sq.isEmpty()) { ++noGrid; continue; }
        auto& cell = cells[sq];
        ++cell.count;
        cell.confirmed = cell.confirmed || isConfirmed(q);
        cell.firstCall = q.call;                 // newest-first → ends oldest
        if (cell.lastCall.isEmpty()) cell.lastCall = q.call;
    }
    for (const auto& c : cells)
        if (c.confirmed) ++confirmedSquares;

    // Refill the combos without re-triggering refresh.
    auto refill = [](QComboBox* box, QSet<QString> values) {
        const QString keep = box->currentText();
        QStringList list(values.begin(), values.end());
        list.sort();
        list.prepend(QStringLiteral("All"));
        const QSignalBlocker block(box);
        box->clear();
        box->addItems(list);
        const int idx = box->findText(keep);
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    refill(m_bandFilter, bands);
    refill(m_modeFilter, modes);

    m_map->setCells(cells);
    m_countLabel->setText(QString::number(cells.size()));
    m_statsLabel->setText(
        QString("squares worked · %1 confirmed · %2 QSOs scanned · %3 with no grid")
            .arg(confirmedSquares).arg(scanned).arg(noGrid));
}

} // namespace ShackLog
