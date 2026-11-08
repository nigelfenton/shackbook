#pragma once

// Band.h — frequency to ADIF band name, with no dependencies beyond QString.
//
// The table lived in LogbookModel::bandFromFreqMhz(), which still exists and
// now delegates here. It moved so lightweight code (SpotIndex, for N1MM's
// per-band spot deletes, #11) can ask "which band?" without pulling in the
// SQL-backed logbook.

#include <QString>

namespace ShackBook {

// ADIF band name ("20m", "70cm", ...) for a frequency in MHz, or an empty
// string when it falls outside every amateur band.
inline QString bandForMhz(double mhz)
{
    struct B { double lo, hi; const char* name; };
    static const B bands[] = {
        { 0.1357,  0.1378,  "2200m" },
        { 0.472,   0.479,   "630m"  },
        { 1.8,     2.0,     "160m"  },
        { 3.5,     4.0,     "80m"   },
        { 5.06,    5.45,    "60m"   },
        { 7.0,     7.3,     "40m"   },
        { 10.1,    10.15,   "30m"   },
        { 14.0,    14.35,   "20m"   },
        { 18.068,  18.168,  "17m"   },
        { 21.0,    21.45,   "15m"   },
        { 24.89,   24.99,   "12m"   },
        { 28.0,    29.7,    "10m"   },
        { 50.0,    54.0,    "6m"    },
        { 70.0,    70.5,    "4m"    },
        { 144.0,   148.0,   "2m"    },
        { 222.0,   225.0,   "1.25m" },
        { 420.0,   450.0,   "70cm"  },
        { 902.0,   928.0,   "33cm"  },
        { 1240.0,  1300.0,  "23cm"  },
        { 2300.0,  2450.0,  "13cm"  },
        { 3300.0,  3500.0,  "9cm"   },
        { 5650.0,  5925.0,  "6cm"   },
        { 10000.0, 10500.0, "3cm"   },
    };
    for (const auto& b : bands) {
        if (mhz >= b.lo && mhz <= b.hi) return QString::fromLatin1(b.name);
    }
    return {};
}

} // namespace ShackBook
