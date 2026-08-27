#pragma once

// WorldMap — the shared pieces of ShackBook's equirectangular world map:
// the coarse continent outlines, the palette, and Maidenhead conversion.
//
// Extracted from GridMapDialog so the Session Map (#3) draws the SAME world
// rather than carrying a second copy of the geometry and a second projection
// that could drift out of step with it. GridMapDialog keeps its own lattice
// and cell painting; only what is genuinely common lives here.
//
// Projection is plain equirectangular: x = (lon+180)/360 * w,
// y = (90-lat)/180 * h. Deliberately coarse — this orients the eye, it is
// not a chart.

#include <QColor>
#include <QPointF>
#include <QRegularExpression>
#include <QString>
#include <QVector>

namespace ShackBook {
namespace world {

using Pt = QPointF;

// Palette shared by both maps so they read as one family.
inline const QColor kSea      (0x0a, 0x10, 0x1e);
inline const QColor kLand     (0x14, 0x1e, 0x30);
inline const QColor kLandEdge (0x20, 0x30, 0x48);
inline const QColor kFieldLine(0x1c, 0x2a, 0x40);

// Deliberately coarse continent outlines, (lon, lat) vertex lists.
// Hand-laid like the section cartogram — orientation, not navigation.
using Pt = QPointF;
inline const QVector<QVector<Pt>> kLand110 = {
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


// Maidenhead locator -> lat/lon at the CENTRE of the square. Handles 4- and
// 6-character grids ("FM19", "FM19ka"); returns false on malformed input.
// Lifted from AprsStationModel.cpp, which had the only correct copy.
inline bool gridToLatLon(const QString& gridIn, double* lat, double* lon)
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
        lo += (2.0 / 24.0) / 2.0;
        la += (1.0 / 24.0) / 2.0;
    } else {
        lo += 1.0;
        la += 0.5;
    }
    *lat = la;
    *lon = lo;
    return true;
}

} // namespace world
} // namespace ShackBook
