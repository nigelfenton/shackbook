#pragma once

// N1mmSpotParser — one N1MM+ / DXLog bandmap spot from a UDP datagram (#11).
//
// N1MM+ and DXLog broadcast their bandmap as one small XML document per UDP
// datagram, in the SmartSDR-CAT-compatible "spot" format:
//
//   <spot>
//     <app>N1MM</app>
//     <StationName>RUN1</StationName>
//     <dxcall>K1ABC</dxcall>
//     <frequency>14025.1</frequency>          <- kHz, not MHz
//     <spottercall>W1XYZ</spottercall>
//     <comment>CQ</comment>
//     <action>add</action>                    <- or "delete"; absent means add
//     <mode>CW</mode>
//     <statuslist>single mult</statuslist>    <- or <status>
//     <timestamp>2026-10-05 14:02:11</timestamp>
//   </spot>
//
// Pure: no sockets, so every case is unit tested without a network. Written
// for ShackBook from the protocol, not ported from any other implementation.

#include "SpotData.h"

#include <QByteArray>
#include <QString>

namespace ShackBook {

struct N1mmSpot {
    SpotData spot;          // call, freqMhz, mode, comment, status, spotter, source
    bool     remove{false}; // <action>delete</action>
    QString  stationName;   // which logger PC sent it (multi-op)
};

enum class N1mmParse {
    Spot,       // a usable spot; `out` is filled
    NotSpot,    // well-formed XML, but another N1MM document (RadioInfo, contactinfo, ...)
    Malformed,  // not XML, cut short, or a spot without a usable call or frequency
};

N1mmParse parseN1mmSpot(const QByteArray& datagram, N1mmSpot& out);

// Reduce a <statuslist>/<status> token string ("single mult", "new qso") to the
// one flag an operator most needs to notice, by priority:
// bust > dupe > mult > cq > busy > qtc > new > "".
QString n1mmStatusFlag(const QString& statusList);

} // namespace ShackBook
