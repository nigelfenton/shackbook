#pragma once

// AdifReader — tolerant ADIF 3.x record parsing, shared domain logic.
//
// Used by the desktop importer (LogbookModel::importAdif).  The server's
// WsjtxAdifReceiver currently carries a private copy of the same parsing
// logic; consolidating it onto this reader is a known follow-up, kept out
// of the importer change so deployed FD infrastructure isn't churned.
//
// Tolerances: case-insensitive tags, optional file header (anything before
// <EOH>), unknown / APP_* fields ignored, whitespace between fields
// ignored.  Field length is taken as BYTES of the UTF-8 stream — matching
// ShackBook's own exporter; pure-ASCII files (N3FJP, WSJT-X) are unaffected
// either way.

#include "Qso.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace ShackBook {
namespace Adif {

// Scan the next ADIF record starting at byte offset `pos` in `data`,
// advancing `pos` past the consumed input.  Header fields (before <EOH>)
// are discarded.  Returns true with the record's fields (lowercase tag →
// trimmed value) when a complete <EOR>-terminated record was read; false
// when no further complete record exists.
bool nextRecord(const QByteArray& data, qsizetype& pos,
                QHash<QString, QString>* fields);

// Construct a Qso from a lowercase-keyed ADIF field hash, applying the
// same normalizations as live ingest: CALL/MODE/SUBMODE uppercased, BAND
// lowercased, deprecated MODE:USB / MODE:LSB folded to SSB + SUBMODE,
// BAND derived from FREQ when absent.
Qso qsoFromFields(const QHash<QString, QString>& fields);

} // namespace Adif
} // namespace ShackBook
