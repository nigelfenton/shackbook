#include "QsoPartyDialog.h"

#include "CountyProgress.h"
#include "LogbookModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ShackLog {

namespace {

// Worked-but-unconfirmed, worked-and-confirmed. Deliberately the same two
// greens the Grid Map uses for the same meaning, so the two views read as
// one family rather than two colour schemes.
const QColor kWorked   (0x0e, 0x7a, 0x33);
const QColor kConfirmed(0x1d, 0xb9, 0x54);

QString describeOccurrence(const ContestOccurrence& o, const QDate& today)
{
    const QString when = o.start == o.end
        ? o.start.toString(QStringLiteral("ddd d MMM"))
        : QStringLiteral("%1-%2").arg(o.start.toString(QStringLiteral("ddd d")),
                                      o.end.toString(QStringLiteral("ddd d MMM")));
    if (o.runsOn(today))
        return QObject::tr("%1 — RUNNING NOW (%2)").arg(o.event.name, when);
    const int days = o.daysUntil(today);
    if (days == 1) return QObject::tr("%1 — tomorrow (%2)").arg(o.event.name, when);
    return QObject::tr("%1 — in %2 days (%3)").arg(o.event.name).arg(days).arg(when);
}

} // namespace

QsoPartyDialog::QsoPartyDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle(tr("QSO Party"));
    resize(760, 620);

    m_counties.load(QStringLiteral(":/data/counties.dat"));
    m_contests.load(QStringLiteral(":/data/contests.dat"));
    m_calendar.load(QStringLiteral(":/data/calendar.dat"));

    auto* root = new QVBoxLayout(this);

    // ── What is on ────────────────────────────────────────────────────
    m_whatsOn = new QLabel(this);
    m_whatsOn->setWordWrap(true);
    m_whatsOn->setTextFormat(Qt::PlainText);
    root->addWidget(m_whatsOn);

    // ── Party picker ──────────────────────────────────────────────────
    auto* pick = new QHBoxLayout;
    pick->addWidget(new QLabel(tr("Party:"), this));
    m_partyPicker = new QComboBox(this);
    pick->addWidget(m_partyPicker, 1);
    m_thisContestOnly = new QCheckBox(tr("Only QSOs logged under this contest"), this);
    // Off by default: for a county award "worked" means ever, which is the
    // more common question outside the party weekend itself.
    m_thisContestOnly->setChecked(false);
    pick->addWidget(m_thisContestOnly);
    root->addLayout(pick);

    m_provenance = new QLabel(this);
    m_provenance->setWordWrap(true);
    root->addWidget(m_provenance);

    m_summary = new QLabel(this);
    QFont sf = m_summary->font();
    sf.setBold(true);
    m_summary->setFont(sf);
    root->addWidget(m_summary);

    // ── The table ─────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("County"), tr("QSOs"), tr("Status")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    root->addWidget(m_table, 1);

    // ── Needed list ───────────────────────────────────────────────────
    // Listed, not just counted: a count says how far there is to go, a list
    // says what to chase. Selectable so it can be pasted into a spot or a
    // club chat.
    root->addWidget(new QLabel(tr("Still needed:"), this));
    m_needed = new QPlainTextEdit(this);
    m_needed->setReadOnly(true);
    m_needed->setMaximumHeight(90);
    root->addWidget(m_needed);

    m_unmatched = new QLabel(this);
    m_unmatched->setWordWrap(true);
    root->addWidget(m_unmatched);

    connect(m_partyPicker, &QComboBox::currentIndexChanged, this, [this](int) {
        showParty(m_partyPicker->currentData().toString());
    });
    connect(m_thisContestOnly, &QCheckBox::toggled, this, [this](bool) {
        showParty(m_partyPicker->currentData().toString());
    });

    rebuildPartyList();
    refresh();
}

void QsoPartyDialog::rebuildPartyList()
{
    m_partyPicker->clear();
    for (const ContestDef& d : m_contests.countyParties())
        m_partyPicker->addItem(QStringLiteral("%1 (%2)").arg(d.name, d.countyState),
                               d.id);

    // Preselect whatever is running today, so opening this during a party
    // lands on the right one without a click.
    const QDate today = QDate::currentDate();
    for (const ContestOccurrence& o : m_calendar.runningOn(today)) {
        const int idx = m_partyPicker->findData(o.event.contestId);
        if (idx >= 0) { m_partyPicker->setCurrentIndex(idx); break; }
    }
}

void QsoPartyDialog::updateWhatsOn()
{
    const QDate today = QDate::currentDate();
    QStringList lines;

    const auto running = m_calendar.runningOn(today);
    for (const ContestOccurrence& o : running)
        lines << describeOccurrence(o, today);

    for (const ContestOccurrence& o : m_calendar.upcoming(today, 3)) {
        bool alreadyListed = false;
        for (const ContestOccurrence& r : running)
            if (r.event.contestId == o.event.contestId) alreadyListed = true;
        if (!alreadyListed) lines << describeOccurrence(o, today);
    }

    m_whatsOn->setText(lines.isEmpty()
        ? tr("Nothing in the bundled calendar is running or coming up.")
        : tr("On now / coming up:\n  %1").arg(lines.join(QStringLiteral("\n  "))));
}

void QsoPartyDialog::refresh()
{
    updateWhatsOn();
    showParty(m_partyPicker->currentData().toString());
}

void QsoPartyDialog::showParty(const QString& contestId)
{
    m_table->setRowCount(0);
    m_needed->clear();
    m_unmatched->clear();

    const ContestDef def = m_contests.find(contestId);
    if (!def.isValid() || !m_model) {
        m_summary->setText(tr("No county party selected."));
        m_provenance->clear();
        return;
    }

    // State the definition's provenance where the operator will see it, not
    // only in the data file. An unconfirmed exchange is fine for a chase
    // view — being wrong costs a mis-labelled column — but they should know.
    m_provenance->setText(def.exchangeConfirmed
        ? QString()
        : tr("⚠ This party's exchange has not been confirmed against the "
             "sponsor's published rules. Fine for chasing counties; check "
             "before trusting it for a submitted log."));

    LogbookFilter filter;
    if (m_thisContestOnly->isChecked()) filter.contestId = def.id;
    const auto progress = countyProgress(m_counties, def.countyState,
                                         m_model->queryQsos(filter));

    m_summary->setText(tr("%1 — %2 of %3 counties worked, %4 confirmed, %5 needed")
        .arg(def.name)
        .arg(progress.worked).arg(progress.total)
        .arg(progress.confirmed).arg(progress.needed()));

    m_table->setRowCount(progress.counties.size());
    QStringList needed;
    int row = 0;
    for (const CountyStatus& c : progress.counties) {
        auto* nameItem = new QTableWidgetItem(c.name);
        auto* qsoItem  = new QTableWidgetItem(c.worked > 0 ? QString::number(c.worked)
                                                           : QString());
        auto* statItem = new QTableWidgetItem(
            c.worked == 0    ? tr("needed")
          : c.confirmed      ? tr("confirmed")
                             : tr("worked"));
        qsoItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        if (c.worked > 0) {
            const QColor fg = c.confirmed ? kConfirmed : kWorked;
            for (auto* it : {nameItem, qsoItem, statItem}) it->setForeground(fg);
        }
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, qsoItem);
        m_table->setItem(row, 2, statItem);
        ++row;

        if (c.worked == 0) needed << c.name;
    }
    m_needed->setPlainText(needed.join(QStringLiteral(", ")));

    // Say what could not be placed rather than dropping it silently, the
    // same posture awardsSummary() takes with wasBogus. These are real
    // mis-logged QSOs, and the operator is the only one who can fix them.
    if (!progress.unmatched.isEmpty()) {
        QStringList bits;
        for (const auto& u : progress.unmatched)
            bits << QStringLiteral("\"%1\" x%2").arg(u.first).arg(u.second);
        m_unmatched->setText(
            tr("⚠ %n county value(s) claim %1 but match no county there — "
               "check these QSOs: %2", nullptr, progress.unmatched.size())
            .arg(def.countyState, bits.join(QStringLiteral(", "))));
    }
}

} // namespace ShackLog
