#pragma once

// QSO record for the ShackLog logbook.
//
// Field names follow ADIF 3.x conventions exactly — CALL, QSO_DATE, TIME_ON,
// TIME_OFF, BAND, FREQ, MODE, RST_SENT, RST_RCVD, etc. — so ADIF export is a
// straight 1-to-1 dump per non-empty field.  Times and dates are stored as
// raw ADIF strings (YYYYMMDD / HHMMSS UTC) rather than QDateTime to preserve
// the exact form the user entered (HHMM with no seconds is valid ADIF).
//
// All string fields default to empty; numeric fields default to 0 / 0.0.
// Where 0 happens to be a valid value (e.g. TX_PWR == 0 W is meaningless on
// HF, SRX/STX == 0 is invalid as a contest serial), 0 is the "not recorded"
// sentinel.

#include <QString>

namespace ShackLog {

struct Qso {
    qint64 id{-1};                       // -1 == not yet inserted

    // ── Essentials ────────────────────────────────────────────────────
    QString call;
    QString qsoDate;                     // ADIF YYYYMMDD UTC
    QString timeOn;                      // ADIF HHMMSS or HHMM UTC
    QString timeOff;
    QString band;
    double  freq{0.0};                   // MHz
    QString mode;
    QString submode;
    QString rstSent;
    QString rstRcvd;

    // ── Other-station info ────────────────────────────────────────────
    QString name;
    QString qth;
    QString gridsquare;
    int     dxcc{0};
    QString country;
    QString state;
    QString cnty;
    QString cont;                        // continent code: NA, SA, EU, AF, AS, OC, AN
    int     cqz{0};
    int     ituz{0};

    // ── My-station info (snapshotted per-QSO) ─────────────────────────
    QString myCall;                      // ADIF STATION_CALLSIGN
    QString myGridsquare;
    QString myState;
    double  txPwr{0.0};                  // W; 0.0 == not recorded
    QString myOperator;                  // ADIF OPERATOR — only if differs from station

    // ── Contest fields ────────────────────────────────────────────────
    QString contestId;
    int     srx{0};
    int     stx{0};
    QString srxString;
    QString stxString;

    // ── Multi-station (server only — added in schema v2) ───────────────
    QString station;                     // e.g. "STN-1"; empty for non-FD use

    // ── Notes ─────────────────────────────────────────────────────────
    QString comment;
    QString notes;

    // ── QSL / confirmation flags (Y/N/R/I) ────────────────────────────
    QString qslSent;
    QString qslRcvd;
    QString lotwSent;
    QString lotwRcvd;
    QString lotwSdate;                   // ADIF LOTW_QSLSDATE (uploaded on)
    QString lotwRdate;                   // ADIF LOTW_QSLRDATE (confirmed on)
    QString eqslSent;
    QString eqslRcvd;

    // ── Bookkeeping ──────────────────────────────────────────────────
    QString createdAt;                   // ISO-8601 UTC
    QString updatedAt;
};

} // namespace ShackLog
