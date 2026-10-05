#pragma once

// ContestLayout — what the entry form should emphasise for one contest (#18).
//
// Derived from ContestDef, never configured separately: a state party wants
// county, a serial-number contest wants a serial, and ContestDef already says
// which. Building a second notion of "what does this contest need" is what #4
// argued against, and Super Check Partial and Cabrillo validation will want
// the same answers.
//
// Pure: no widgets. ContestLayoutController applies the result to the real
// form, and reverts it exactly.
//
// Decisions recorded on #18:
//   • unused fields are de-emphasised (dimmed, skipped by Tab), never hidden,
//     so a field can never vanish with a value in it;
//   • switching layout never touches typed values; instead missingOnSave()
//     says which received fields the contest needs and are empty, so the form
//     and the stored record agree.

#include "ContestDef.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace ShackBook {

// The quick-entry form's fields.
enum class EntrySlot {
    Call,
    RstSent,
    RstRcvd,
    Comment,
    Stx,        // sent serial
    StxText,    // sent exchange text (county, state, section, ...)
    Srx,        // received serial
    SrxText,    // received exchange text
};

inline size_t qHash(EntrySlot s, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<int>(s), seed);
}

struct ContestLayout {
    QString contestId;
    QString name;
    // Fields in the order Tab should visit them. Call first, then what is
    // RECEIVED (typed every QSO), then what is SENT (usually prefilled).
    QVector<EntrySlot> tabOrder;
    // Fields this contest doesn't use: shown dimmed and skipped by Tab.
    QSet<EntrySlot> deemphasised;
    // Labels for the exchange-text fields, e.g. "COUNTY"; empty keeps the
    // everyday label.
    QString stxTextLabel;
    QString srxTextLabel;
    // Received fields that must not be empty when a QSO is saved.
    QVector<EntrySlot> requiredOnSave;

    bool isValid() const { return !contestId.isEmpty(); }
};

ContestLayout contestLayoutFor(const ContestDef& def);

// Which of layout.requiredOnSave are empty in `values` (keyed by slot, already
// trimmed or not). Order follows requiredOnSave.
QVector<EntrySlot> missingOnSave(const ContestLayout& layout,
                                 const QHash<EntrySlot, QString>& values);

// Operator-facing name of a field, for messages: "County", "Serial", ...
// Uses the layout's labels where it has them.
QString entrySlotName(const ContestLayout& layout, EntrySlot slot);

} // namespace ShackBook
