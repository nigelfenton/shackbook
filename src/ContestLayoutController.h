#pragma once

// ContestLayoutController — apply a ContestLayout to the real entry form, and
// put the form back EXACTLY as it was (#18).
//
// The hard part of a contest layout is not drawing it, it is the revert: tab
// order, focus behaviour, styles and labels must come back with no residue.
// So the everyday form is snapshotted once, after it is built, and revert()
// restores that snapshot rather than trying to undo each change. Applying a
// second contest's layout reverts the first one before applying.
//
// Never hides or disables anything: unused fields are dimmed and skipped by
// Tab (Qt::ClickFocus), so they stay usable and a value can never vanish.
//
// Owns no widgets. tests/contest_layout_test.cpp checks the exact revert
// against real widgets.

#include "ContestLayout.h"

#include <QHash>
#include <QPointer>
#include <QString>
#include <QVector>

class QLabel;
class QWidget;

namespace ShackBook {

class ContestLayoutController {
public:
    // Register each form field and its label, then call captureEveryday()
    // once the form is fully built.
    void registerSlot(EntrySlot slot, QWidget* field, QLabel* label);
    void captureEveryday();

    void apply(const ContestLayout& layout);
    void revert();

    bool isApplied() const { return m_applied; }
    const ContestLayout& current() const { return m_layout; }

    // Mark a field as missing on save (a red outline), or clear the mark.
    // Recomputed from the snapshot, so it never leaks into a revert.
    void setMissing(EntrySlot slot, bool missing);
    void clearMissing();

private:
    struct Slot {
        QPointer<QWidget> field;
        QPointer<QLabel>  label;
        // Everyday snapshot
        QString           fieldStyle;
        QString           labelStyle;
        QString           labelText;
        Qt::FocusPolicy   focusPolicy{Qt::StrongFocus};
        bool              missing{false};
    };

    void restyle(EntrySlot slot);

    QHash<EntrySlot, Slot> m_slots;
    QVector<EntrySlot>     m_everydayOrder;   // tab order of the registered fields
    ContestLayout          m_layout;
    bool                   m_applied{false};
    bool                   m_captured{false};
};

} // namespace ShackBook
