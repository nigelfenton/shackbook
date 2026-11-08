#include "ContestLayoutController.h"

#include <QLabel>
#include <QWidget>

#include <algorithm>

namespace ShackBook {

namespace {
// Appended to the everyday style, so the dimming wins without discarding it.
const char* const kDimField =
    " QLineEdit { color: #4f5f73; background: #0a1420; border-color: #16202e; }";
const char* const kDimLabel = " QLabel { color: #3a4758; }";
const char* const kMissingField = " QLineEdit { border: 1px solid #e05252; }";

// How far along the window's focus chain a widget sits, for ordering.
int chainPosition(QWidget* w)
{
    if (!w || !w->window()) return -1;
    QWidget* start = w->window();
    int i = 0;
    for (QWidget* p = start->nextInFocusChain(); p && p != start; p = p->nextInFocusChain(), ++i)
        if (p == w) return i;
    return -1;
}
} // namespace

void ContestLayoutController::registerSlot(EntrySlot slot, QWidget* field, QLabel* label)
{
    Slot s;
    s.field = field;
    s.label = label;
    m_slots.insert(slot, s);
}

void ContestLayoutController::captureEveryday()
{
    m_everydayOrder.clear();
    for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
        Slot& s = it.value();
        if (s.field) {
            s.fieldStyle = s.field->styleSheet();
            s.focusPolicy = s.field->focusPolicy();
        }
        if (s.label) {
            s.labelStyle = s.label->styleSheet();
            s.labelText = s.label->text();
        }
        m_everydayOrder << it.key();
    }
    // The everyday tab order is whatever the focus chain says, not the order
    // fields were registered in.
    std::sort(m_everydayOrder.begin(), m_everydayOrder.end(), [this](EntrySlot a, EntrySlot b) {
        return chainPosition(m_slots.value(a).field) < chainPosition(m_slots.value(b).field);
    });
    m_captured = true;
}

void ContestLayoutController::apply(const ContestLayout& layout)
{
    if (!m_captured || !layout.isValid()) return;
    if (m_applied) revert();
    m_layout = layout;
    m_applied = true;

    for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
        Slot& s = it.value();
        if (!s.field) continue;
        const bool dim = layout.deemphasised.contains(it.key());
        // ClickFocus: Tab skips it, a click still edits it. Never hidden or
        // disabled, so a value in it is always visible and editable.
        s.field->setFocusPolicy(dim ? Qt::ClickFocus : s.focusPolicy);
        restyle(it.key());
    }

    // Name the exchange boxes after what goes in them, in the form's own arrow
    // style (RST→ sent, ←RST received): "COUNTY→" and "←COUNTY".
    auto relabel = [this](EntrySlot slot, const QString& text) {
        const Slot s = m_slots.value(slot);
        if (s.label) s.label->setText(text);
    };
    if (!layout.stxTextLabel.isEmpty())
        relabel(EntrySlot::StxText, layout.stxTextLabel + QStringLiteral("→"));
    if (!layout.srxTextLabel.isEmpty())
        relabel(EntrySlot::SrxText, QStringLiteral("←") + layout.srxTextLabel);

    // Tab order: the layout's fields in its order.
    QWidget* prev = nullptr;
    for (EntrySlot slot : layout.tabOrder) {
        QWidget* w = m_slots.value(slot).field;
        if (!w) continue;
        if (prev) QWidget::setTabOrder(prev, w);
        prev = w;
    }
}

void ContestLayoutController::revert()
{
    if (!m_captured) return;
    for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
        Slot& s = it.value();
        s.missing = false;
        if (s.field) {
            s.field->setStyleSheet(s.fieldStyle);
            s.field->setFocusPolicy(s.focusPolicy);
        }
        if (s.label) {
            s.label->setStyleSheet(s.labelStyle);
            s.label->setText(s.labelText);
        }
    }
    QWidget* prev = nullptr;
    for (EntrySlot slot : m_everydayOrder) {
        QWidget* w = m_slots.value(slot).field;
        if (!w) continue;
        if (prev) QWidget::setTabOrder(prev, w);
        prev = w;
    }
    m_layout = ContestLayout{};
    m_applied = false;
}

void ContestLayoutController::setMissing(EntrySlot slot, bool missing)
{
    auto it = m_slots.find(slot);
    if (it == m_slots.end()) return;
    it->missing = missing;
    restyle(slot);
}

void ContestLayoutController::clearMissing()
{
    for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
        if (!it->missing) continue;
        it->missing = false;
        restyle(it.key());
    }
}

void ContestLayoutController::restyle(EntrySlot slot)
{
    const Slot s = m_slots.value(slot);
    const bool dim = m_applied && m_layout.deemphasised.contains(slot);
    if (s.field) {
        QString style = s.fieldStyle;
        if (dim) style += QLatin1String(kDimField);
        if (s.missing) style += QLatin1String(kMissingField);
        s.field->setStyleSheet(style);
    }
    if (s.label)
        s.label->setStyleSheet(dim ? s.labelStyle + QLatin1String(kDimLabel) : s.labelStyle);
}

} // namespace ShackBook
