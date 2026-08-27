#pragma once

// QsoPartyDialog — worked/needed counties and what is on (#4, items 2 and 3).
//
// One window rather than two, because during a party these are the same
// question: "what is running, and which counties do I still need for it?".
// Splitting them would mean the operator watching two windows while trying
// to work a pileup.
//
// The county table is the part that changes how you operate — it turns
// "have I got Calvert yet?" from a memory question into a glance. Needed
// counties are listed explicitly rather than merely counted, because a
// count tells you how far you have to go and a list tells you what to
// chase.
//
// SCOPE. This is a chase view, not a scoreboard: it shows what has been
// worked and what has not. It does not score, because ContestDef
// deliberately carries no scoring model — see src/ContestDef.h for why
// half a scoring model would be worse than none.

#include "ContestCalendar.h"
#include "ContestDef.h"
#include "CountyList.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTableWidget;

namespace ShackBook {

class LogbookModel;

class QsoPartyDialog : public QDialog {
    Q_OBJECT

public:
    explicit QsoPartyDialog(LogbookModel* model, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    void rebuildPartyList();
    void showParty(const QString& contestId);
    void updateWhatsOn();

    LogbookModel*   m_model{};
    CountyList      m_counties;
    ContestCatalog  m_contests;
    ContestCalendar m_calendar;

    QComboBox*      m_partyPicker{};
    QLabel*         m_whatsOn{};
    QLabel*         m_summary{};
    QLabel*         m_provenance{};
    QTableWidget*   m_table{};
    QPlainTextEdit* m_needed{};
    QLabel*         m_unmatched{};

    // When true, only QSOs carrying this party's contest_id are counted.
    // Off by default: "worked" for a county award means ever, and that is
    // the more common question outside the party weekend itself.
    QCheckBox*      m_thisContestOnly{};
};

} // namespace ShackBook
