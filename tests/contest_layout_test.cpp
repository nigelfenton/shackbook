// QSO party layout (#18): the layout derived from ContestDef, and the
// controller's EXACT revert against real widgets.
//
// ⭐ The revert is the point. The issue says it plainly: tab order, focus,
// styles and labels must return with no residue. So every controller case
// snapshots the whole everyday form first and demands it back byte for byte,
// including after switching contests and after a field was marked missing.
//
// Also pinned: a layout never hides or disables a field, and never touches a
// value that is already typed.
//
// Runs offscreen (QT_QPA_PLATFORM=offscreen, set by CMake and here).

#include "ContestLayout.h"
#include "ContestLayoutController.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QWidget>

#include <cstdio>

using namespace ShackBook;

namespace {

int failures = 0;

void check(bool cond, const char* what)
{
    std::printf("%s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++failures;
}

ContestDef def(const char* id, const char* name,
               QVector<ExchangeField> sent, QVector<ExchangeField> received)
{
    ContestDef d;
    d.id = QString::fromLatin1(id);
    d.name = QString::fromLatin1(name);
    d.sent = sent;
    d.received = received;
    return d;
}

using F = ExchangeField;
using S = EntrySlot;

const ContestDef kMdc = def("MDC-QSO-PARTY", "Maryland-DC QSO Party",
                            {F::Rst, F::County}, {F::Rst, F::County});
const ContestDef kCqp = def("CQP", "California QSO Party",
                            {F::Rst, F::Serial, F::County}, {F::Rst, F::Serial, F::County});

void pureLayout()
{
    std::printf("\n-- layout from ContestDef --\n");

    const ContestLayout mdc = contestLayoutFor(kMdc);
    check(mdc.isValid() && mdc.name == QStringLiteral("Maryland-DC QSO Party"), "a party gives a layout");
    check(mdc.tabOrder == QVector<S>({S::Call, S::RstRcvd, S::SrxText, S::RstSent, S::StxText}),
          "tab order: call, then what's received, then what's sent");
    check(mdc.deemphasised == QSet<S>({S::Comment, S::Stx, S::Srx}),
          "a county party dims the serial boxes and the comment");
    check(mdc.srxTextLabel == QStringLiteral("COUNTY") && mdc.stxTextLabel == QStringLiteral("COUNTY"),
          "the exchange boxes are named COUNTY");
    check(mdc.requiredOnSave == QVector<S>({S::RstRcvd, S::SrxText}),
          "the received RST and county are needed on save");

    const ContestLayout cqp = contestLayoutFor(kCqp);
    check(cqp.tabOrder == QVector<S>({S::Call, S::RstRcvd, S::Srx, S::SrxText,
                                      S::RstSent, S::Stx, S::StxText}),
          "a serial party keeps the serial boxes, in exchange order");
    check(cqp.deemphasised == QSet<S>({S::Comment}), "and dims only the comment");
    check(cqp.requiredOnSave == QVector<S>({S::RstRcvd, S::Srx, S::SrxText}), "serial is needed on save");

    const ContestLayout two = contestLayoutFor(def("X", "Two text items", {F::Name, F::State},
                                                   {F::Name, F::State}));
    check(two.srxTextLabel == QStringLiteral("NAME/STATE"), "two text items share one box, named for both");
    check(two.tabOrder.count(S::SrxText) == 1, "and appear once in tab order");

    check(!contestLayoutFor(ContestDef{}).isValid(), "no definition, no layout");

    std::printf("\n-- missing on save --\n");
    QHash<S, QString> v{{S::RstRcvd, QStringLiteral("59")}, {S::SrxText, QString()}};
    check(missingOnSave(mdc, v) == QVector<S>({S::SrxText}), "an empty county is reported");
    v[S::SrxText] = QStringLiteral("   ");
    check(missingOnSave(mdc, v) == QVector<S>({S::SrxText}), "spaces count as empty");
    v[S::SrxText] = QStringLiteral("ANNE");
    check(missingOnSave(mdc, v).isEmpty(), "a filled exchange is fine");
    check(entrySlotName(mdc, S::SrxText) == QStringLiteral("County"), "the message calls it County");
    check(entrySlotName(cqp, S::Srx) == QStringLiteral("Serial received"), "and a serial by name");
}

// The everyday form, built in the same field order as MainWindow's.
struct Form {
    QWidget root;
    QHash<S, QLineEdit*> field;
    QHash<S, QLabel*> label;
    ContestLayoutController ctl;

    Form()
    {
        auto* row = new QHBoxLayout(&root);
        const QList<QPair<S, QString>> slots = {
            {S::Call, QStringLiteral("CALL")}, {S::RstSent, QStringLiteral("RST→")},
            {S::RstRcvd, QStringLiteral("←RST")}, {S::Comment, QStringLiteral("COMMENT")},
            {S::Stx, QStringLiteral("STX")}, {S::StxText, QStringLiteral("STX exch")},
            {S::Srx, QStringLiteral("SRX")}, {S::SrxText, QStringLiteral("SRX exch")}};
        int i = 0;
        for (const auto& [slot, text] : slots) {
            auto* l = new QLabel(text);
            l->setStyleSheet(QStringLiteral("QLabel { color: #6b80%1; }").arg(10 + i));
            auto* e = new QLineEdit;
            e->setStyleSheet(QStringLiteral("QLineEdit { background: #0d1e%1; }").arg(30 + i));
            row->addWidget(l);
            row->addWidget(e);
            field[slot] = e;
            label[slot] = l;
            ctl.registerSlot(slot, e, l);
            ++i;
        }
        // An everyday field with a non-default focus policy, so the revert has
        // to restore the real value rather than assume StrongFocus.
        field[S::RstSent]->setFocusPolicy(Qt::WheelFocus);
        ctl.captureEveryday();
    }

    // Tab order of the form's fields, as the focus chain has it.
    QVector<QWidget*> chain() const
    {
        QVector<QWidget*> out;
        const QWidget* start = &root;
        for (QWidget* w = root.nextInFocusChain(); w && w != start; w = w->nextInFocusChain())
            for (QLineEdit* e : field)
                if (w == e) out << w;
        return out;
    }

    // Everything the revert must restore.
    QStringList snapshot() const
    {
        QStringList s;
        for (auto it = field.constBegin(); it != field.constEnd(); ++it) {
            const QLabel* l = label.value(it.key());
            s << QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                     .arg(static_cast<int>(it.key()))
                     .arg(it.value()->styleSheet())
                     .arg(int(it.value()->focusPolicy()))
                     .arg(l->text(), l->styleSheet())
                     .arg(it.value()->isHidden())
                     .arg(it.value()->isEnabled());
        }
        s.sort();
        for (QWidget* w : chain()) s << QString::number(reinterpret_cast<quintptr>(w));
        return s;
    }
};

void controller()
{
    std::printf("\n-- controller: apply --\n");
    Form f;
    f.field[S::Comment]->setText(QStringLiteral("typed before the layout"));
    const QStringList everyday = f.snapshot();

    const ContestLayout mdc = contestLayoutFor(kMdc);
    f.ctl.apply(mdc);
    check(f.ctl.isApplied() && f.ctl.current().contestId == QStringLiteral("MDC-QSO-PARTY"), "the layout is on");
    check(f.field[S::Comment]->focusPolicy() == Qt::ClickFocus
              && f.field[S::Stx]->focusPolicy() == Qt::ClickFocus
              && f.field[S::Srx]->focusPolicy() == Qt::ClickFocus,
          "unused fields are skipped by Tab, still clickable");
    check(f.field[S::RstSent]->focusPolicy() == Qt::WheelFocus
              && f.field[S::SrxText]->focusPolicy() == Qt::StrongFocus,
          "used fields keep their own focus behaviour");
    bool shown = true, enabled = true;
    for (QLineEdit* e : f.field) { shown = shown && !e->isHidden(); enabled = enabled && e->isEnabled(); }
    check(shown && enabled, "nothing is hidden or disabled");
    check(f.field[S::Comment]->text() == QStringLiteral("typed before the layout"),
          "a value already typed is left alone");
    check(f.field[S::Comment]->styleSheet().startsWith(QStringLiteral("QLineEdit { background: #0d1e33; }"))
              && f.field[S::Comment]->styleSheet() != QStringLiteral("QLineEdit { background: #0d1e33; }"),
          "a dimmed field keeps its own style, with the dimming added");
    check(f.label[S::SrxText]->text() == QStringLiteral("←COUNTY")
              && f.label[S::StxText]->text() == QStringLiteral("COUNTY→"),
          "the exchange boxes are labelled for the party");

    const QVector<QWidget*> c = f.chain();
    QVector<QWidget*> used;
    for (QWidget* w : c)
        for (S s : mdc.tabOrder)
            if (w == f.field[s]) used << w;
    QVector<QWidget*> want;
    for (S s : mdc.tabOrder) want << f.field[s];
    check(used == want, "Tab visits the party's fields in the party's order");

    std::printf("\n-- controller: exact revert --\n");
    f.ctl.revert();
    check(!f.ctl.isApplied(), "the layout is off");
    check(f.snapshot() == everyday, "styles, focus, labels and tab order are back exactly");
    check(f.field[S::Comment]->text() == QStringLiteral("typed before the layout"), "the typed value survived");

    f.ctl.apply(mdc);
    f.ctl.apply(contestLayoutFor(kCqp));
    check(f.ctl.current().contestId == QStringLiteral("CQP")
              && f.field[S::Srx]->focusPolicy() == Qt::StrongFocus,
          "switching contests replaces the layout cleanly");
    f.ctl.revert();
    check(f.snapshot() == everyday, "a revert after switching is still exact");

    std::printf("\n-- controller: missing marker --\n");
    f.ctl.apply(mdc);
    const QString applied = f.field[S::SrxText]->styleSheet();
    f.ctl.setMissing(S::SrxText, true);
    check(f.field[S::SrxText]->styleSheet() != applied, "a missing field is marked");
    f.ctl.clearMissing();
    check(f.field[S::SrxText]->styleSheet() == applied, "clearing the mark restores the layout's style");
    f.ctl.setMissing(S::SrxText, true);
    f.ctl.revert();
    check(f.snapshot() == everyday, "a revert with a field still marked is exact");

    f.ctl.revert();
    check(f.snapshot() == everyday, "reverting when nothing is applied changes nothing");
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    pureLayout();
    controller();

    if (failures == 0) {
        std::printf("\ncontest_layout_test: all checks passed\n");
        return 0;
    }
    std::printf("\ncontest_layout_test: %d check(s) FAILED\n", failures);
    return 1;
}
