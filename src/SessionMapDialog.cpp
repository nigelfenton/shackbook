#include "SessionMapDialog.h"
#include "WorldMap.h"
#include "LogbookModel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace ShackLog {

namespace {

using world::Pt;

// Band colours. Chosen so the bands that dominate a normal evening stay
// separable for the red-green colour blind: the 40/20 pair is amber vs
// teal, not red vs green.
QColor bandColor(const QString& bandIn)
{
    const QString b = bandIn.trimmed().toLower();
    if (b == QLatin1String("160m")) return QColor(0x8b, 0x5c, 0xf6);
    if (b == QLatin1String("80m"))  return QColor(0x60, 0x7d, 0xf6);
    if (b == QLatin1String("60m"))  return QColor(0x3b, 0x9d, 0xd8);
    if (b == QLatin1String("40m"))  return QColor(0xf5, 0xa5, 0x24);
    if (b == QLatin1String("30m"))  return QColor(0xe8, 0x77, 0x2e);
    if (b == QLatin1String("20m"))  return QColor(0x2d, 0xd4, 0xbf);
    if (b == QLatin1String("17m"))  return QColor(0x84, 0xcc, 0x16);
    if (b == QLatin1String("15m"))  return QColor(0xec, 0x48, 0x99);
    if (b == QLatin1String("12m"))  return QColor(0xf4, 0x72, 0xb6);
    if (b == QLatin1String("10m"))  return QColor(0xef, 0x44, 0x44);
    if (b == QLatin1String("6m"))   return QColor(0xfb, 0xbf, 0x24);
    if (b == QLatin1String("2m"))   return QColor(0x22, 0xd3, 0xee);
    if (b == QLatin1String("70cm")) return QColor(0xa7, 0x8b, 0xfa);
    return QColor(0x94, 0xa3, 0xb8);   // unknown / missing band
}

// Great-circle interpolation (slerp on the sphere).
//
// A straight line between two points on an equirectangular map is NOT the
// path the signal took: Maryland to Japan drawn straight runs across the
// Pacific, while the real great circle goes over the pole. Drawing the wrong
// one would misrepresent the exact thing this map exists to show.
void greatCirclePoints(double lat1, double lon1, double lat2, double lon2,
                       QVector<Pt>* out, int segments = 64)
{
    const double d2r = M_PI / 180.0, r2d = 180.0 / M_PI;
    const double p1 = lat1 * d2r, l1 = lon1 * d2r;
    const double p2 = lat2 * d2r, l2 = lon2 * d2r;

    const double cosD = std::sin(p1) * std::sin(p2)
                      + std::cos(p1) * std::cos(p2) * std::cos(l2 - l1);
    const double d = std::acos(qBound(-1.0, cosD, 1.0));

    out->clear();
    if (d < 1e-9) {                       // same point; nothing to draw
        out->append(Pt(lon1, lat1));
        return;
    }
    const double sinD = std::sin(d);
    for (int i = 0; i <= segments; ++i) {
        const double f = double(i) / segments;
        const double a = std::sin((1 - f) * d) / sinD;
        const double b = std::sin(f * d) / sinD;
        const double x = a * std::cos(p1) * std::cos(l1) + b * std::cos(p2) * std::cos(l2);
        const double y = a * std::cos(p1) * std::sin(l1) + b * std::cos(p2) * std::sin(l2);
        const double z = a * std::sin(p1) + b * std::sin(p2);
        out->append(Pt(std::atan2(y, x) * r2d,
                       std::atan2(z, std::sqrt(x * x + y * y)) * r2d));
    }
}

} // namespace

// ---- SessionMapWidget -------------------------------------------------

SessionMapWidget::SessionMapWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(720, 380);
}

void SessionMapWidget::setSession(const QVector<SessionPoint>& points,
                                  double originLat, double originLon,
                                  bool haveOrigin)
{
    m_points = points;
    m_originLat = originLat;
    m_originLon = originLon;
    m_haveOrigin = haveOrigin;
    update();
}

void SessionMapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double lonW = width() / 360.0, latH = height() / 180.0;
    auto toXY = [&](double lon, double lat) {
        return QPointF((lon + 180.0) * lonW, (90.0 - lat) * latH);
    };

    p.fillRect(rect(), world::kSea);

    p.setPen(QPen(world::kLandEdge, 1.0));
    p.setBrush(world::kLand);
    for (const auto& poly : world::kLand110) {
        QPainterPath path;
        path.moveTo(toXY(poly.first().x(), poly.first().y()));
        for (int i = 1; i < poly.size(); ++i)
            path.lineTo(toXY(poly[i].x(), poly[i].y()));
        path.closeSubpath();
        p.drawPath(path);
    }

    // Graticule, faint, for orientation only.
    p.setPen(QPen(world::kFieldLine, 1.0));
    for (int lon = -180; lon <= 180; lon += 30)
        p.drawLine(toXY(lon, -90), toXY(lon, 90));
    for (int lat = -60; lat <= 60; lat += 30)
        p.drawLine(toXY(-180, lat), toXY(180, lat));

    // Paths first, so the contact points sit on top of them.
    if (m_haveOrigin) {
        QVector<Pt> gc;
        for (const SessionPoint& sp : m_points) {
            greatCirclePoints(m_originLat, m_originLon, sp.lat, sp.lon, &gc);
            if (gc.size() < 2) continue;

            QColor c = bandColor(sp.band);
            c.setAlpha(150);
            const QPen pen(c, 1.2);

            // Break the polyline where it crosses the antimeridian, or the
            // segment is drawn straight back across the whole map as a
            // horizontal streak.
            QPainterPath path;
            bool started = false;
            for (int i = 0; i < gc.size(); ++i) {
                if (i > 0 && std::abs(gc[i].x() - gc[i - 1].x()) > 180.0)
                    started = false;
                const QPointF xy = toXY(gc[i].x(), gc[i].y());
                if (!started) { path.moveTo(xy); started = true; }
                else            path.lineTo(xy);
            }
            p.strokePath(path, pen);
        }
    }

    for (const SessionPoint& sp : m_points) {
        const QPointF xy = toXY(sp.lon, sp.lat);
        const QColor c = bandColor(sp.band);
        p.setPen(QPen(c.darker(160), 1.0));
        p.setBrush(c);
        p.drawEllipse(xy, 3.2, 3.2);
    }

    // The station itself, last and distinct.
    if (m_haveOrigin) {
        p.setBrush(QColor(0xff, 0xd1, 0x4d));
        p.setPen(QPen(QColor(0x1a, 0x14, 0x00), 1.5));
        p.drawEllipse(toXY(m_originLon, m_originLat), 4.5, 4.5);
    }
}

// ---- SessionMapDialog -------------------------------------------------

SessionMapDialog::SessionMapDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle(tr("Session Map"));
    resize(900, 520);

    auto* root = new QVBoxLayout(this);

    m_titleLabel = new QLabel(this);
    QFont tf = m_titleLabel->font();
    tf.setPointSizeF(tf.pointSizeF() + 1.5);
    tf.setBold(true);
    m_titleLabel->setFont(tf);
    root->addWidget(m_titleLabel);

    m_map = new SessionMapWidget(this);
    root->addWidget(m_map, 1);

    m_bandsLabel = new QLabel(this);
    m_bandsLabel->setWordWrap(true);
    root->addWidget(m_bandsLabel);

    auto* row = new QHBoxLayout;
    m_prevBtn = new QPushButton(tr("Previous session"), this);
    m_nextBtn = new QPushButton(tr("Next session"), this);
    m_statsLabel = new QLabel(this);
    m_statsLabel->setWordWrap(true);
    row->addWidget(m_prevBtn);
    row->addWidget(m_nextBtn);
    row->addWidget(m_statsLabel, 1);
    root->addLayout(row);

    // m_current counts BACK through time: 0 is the most recent session, so
    // "Previous" increases the index.
    connect(m_prevBtn, &QPushButton::clicked, this,
            [this] { showSession(m_current + 1); });
    connect(m_nextBtn, &QPushButton::clicked, this,
            [this] { showSession(m_current - 1); });

    refresh();
}

void SessionMapDialog::refresh()
{
    if (!m_model) return;
    m_sessions = splitSessions(m_model->queryQsos(), 2, &m_unplaceableTime);
    showSession(0);
}

void SessionMapDialog::showSession(int index)
{
    if (m_sessions.isEmpty()) {
        m_titleLabel->setText(tr("No QSOs to show"));
        m_statsLabel->setText(m_unplaceableTime > 0
            ? tr("%n QSO(s) have no usable date/time and cannot be placed in a session.",
                 nullptr, m_unplaceableTime)
            : QString());
        m_bandsLabel->clear();
        m_map->setSession({}, 0, 0, false);
        m_prevBtn->setEnabled(false);
        m_nextBtn->setEnabled(false);
        return;
    }

    m_current = qBound(0, index, m_sessions.size() - 1);
    const QsoSession& s = m_sessions[m_current];

    double originLat = 0, originLon = 0;
    const bool haveOrigin =
        world::gridToLatLon(m_model->myGridsquare(), &originLat, &originLon);

    QVector<SessionPoint> pts;
    QMap<QString, int> perBand;          // QMap: sorted, so the legend is stable
    int noGrid = 0;
    for (const Qso& q : s.qsos) {
        const QString band = q.band.trimmed();
        perBand[band.isEmpty() ? tr("(no band)") : band]++;

        SessionPoint sp;
        if (!world::gridToLatLon(q.gridsquare, &sp.lat, &sp.lon)) { ++noGrid; continue; }
        sp.call = q.call;
        sp.band = band;
        sp.grid = q.gridsquare;
        pts.append(sp);
    }
    m_map->setSession(pts, originLat, originLon, haveOrigin);

    const QString when = s.start.toString(QStringLiteral("ddd d MMM yyyy"));
    const QString span = QStringLiteral("%1-%2 UTC")
        .arg(s.start.toString(QStringLiteral("HH:mm")),
             s.end.toString(QStringLiteral("HH:mm")));
    m_titleLabel->setText(m_current == 0
        ? tr("Latest session - %1 (%2)").arg(when, span)
        : tr("Session %1 back - %2 (%3)").arg(m_current).arg(when, span));

    QStringList bands;
    for (auto it = perBand.constBegin(); it != perBand.constEnd(); ++it)
        bands << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
    m_bandsLabel->setText(bands.join(QStringLiteral("   ")));

    // Say what could not be placed rather than quietly dropping it - the same
    // honesty the Grid Map scanned count provides. In a real log this is a
    // large fraction: hand-logged SSB contacts often carry no grid at all.
    QStringList notes;
    notes << tr("%n QSO(s)", nullptr, s.count());
    notes << tr("%n plotted", nullptr, pts.size());
    if (noGrid > 0)
        notes << tr("%n without a usable grid - not shown", nullptr, noGrid);
    if (!haveOrigin)
        notes << tr("MY_GRIDSQUARE is not set, so no paths are drawn");
    if (m_unplaceableTime > 0)
        notes << tr("%n log-wide with no usable date/time", nullptr, m_unplaceableTime);
    m_statsLabel->setText(notes.join(QStringLiteral(" | ")));

    m_prevBtn->setEnabled(m_current + 1 < m_sessions.size());
    m_nextBtn->setEnabled(m_current > 0);
}

} // namespace ShackLog
