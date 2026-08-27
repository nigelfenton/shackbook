#pragma once

// AprsDecode — KISS + AX.25 + APRS decoding, pure logic (no Qt sockets), so it
// unit-tests standalone. Fed the raw bytes an AetherSDR (or any) KISS-over-TCP
// TNC server sends, it yields decoded APRS reports.
//
// The layers, outermost first:
//   1. KISS framing  — FEND (0xC0) delimits frames; FESC (0xDB) escapes FEND
//      /FESC in the payload. A data frame's first byte is the type/port byte
//      (low nibble 0 = data); the rest is a raw AX.25 frame (no FCS — the TNC
//      strips it). `kissUnframe` splits a byte buffer into AX.25 payloads.
//   2. AX.25 UI frame — 7-byte dest + 7-byte source (+ optional digipeater
//      path), each addr = 6 shifted ASCII chars + 1 SSID byte; then control
//      (0x03 = UI) + PID (0xF0 = no layer 3); then the information field.
//   3. APRS info field — the first character is the Data Type Identifier.
//      We decode position reports (!, =, @, /) and text messages (:). Enough
//      for an activity display ("who's around, where") and a messaging mailbox.
//
// Sources: the AX.25 v2.2 spec and the APRS Protocol Reference (APRS101.PDF).
// Clean-room — AetherSDR's GPL decoder was a correctness reference only, not
// copied; this stays MIT with the rest of ShackBook.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace ShackBook {
namespace Aprs {

// One decoded AX.25 UI frame, APRS-interpreted where possible.
struct Report {
    // AX.25 addressing
    QString     source;        // e.g. "G0JKN-9"
    QString     destination;   // the AX.25 dest / tocall
    QStringList path;          // digipeater path

    QString     info;          // the raw information field (post-PID)
    char        dataType{'\0'};// APRS Data Type Identifier (info[0])

    // Position (valid() true when a position was decoded)
    bool        hasPosition{false};
    double      latitude{0.0}; // decimal degrees, +N
    double      longitude{0.0};// decimal degrees, +E
    char        symbolTable{'/'};
    char        symbolCode{'-'};
    QString     comment;       // free text after the position

    // APRS message (dataType ':'); hasMessage true when parsed
    bool        hasMessage{false};
    QString     addressee;     // who the message is TO (trimmed, upper)
    QString     messageText;
    QString     messageNo;     // ack id, empty if none requested
    bool        isAck{false};  // this frame is an ack/rej of a prior message

    bool isValid() const { return !source.isEmpty(); }
};

// --- KISS layer ---------------------------------------------------------
// Split a raw KISS byte stream into de-escaped AX.25 payloads. Only type-0
// (data) frames are returned; command frames are skipped. Partial trailing
// data (no closing FEND) is returned via `leftover` so a caller streaming
// from a socket can prepend it to the next read.
QVector<QByteArray> kissUnframe(const QByteArray& in, QByteArray* leftover = nullptr);

// Wrap a raw AX.25 frame in a KISS data frame (for TX — the mailbox send path).
QByteArray kissFrameData(const QByteArray& ax25);

// --- AX.25 + APRS layer -------------------------------------------------
// Decode one raw AX.25 UI frame (no FCS) into a Report. Returns nullopt if the
// frame is too short / not a UI frame / addresses malformed.
std::optional<Report> decodeAx25(const QByteArray& ax25);

// Build a raw AX.25 UI frame (no FCS) from parts — used to TX an APRS packet.
// `dest` is typically "APZ001" (an experimental tocall). Returns empty on bad
// input (e.g. a callsign that won't fit the 6-char AX.25 address field).
QByteArray buildAx25Ui(const QString& source, const QString& dest,
                       const QStringList& path, const QByteArray& info);

// Build the APRS message info field: ":ADDRESSEE :text{msgno". The addressee is
// space-padded to the APRS-standard 9 characters. msgNo empty = no ack request.
QByteArray buildMessageInfo(const QString& addressee, const QString& text,
                            const QString& msgNo = QString());

} // namespace Aprs
} // namespace ShackBook
