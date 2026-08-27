#include "EditDialog.h"

#include "LogbookModel.h"

#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QMessageBox>

namespace ShackBook {

namespace {
const QStringList kQslStates = { "", "Y", "N", "R", "I" };
}

EditDialog::EditDialog(LogbookModel* model, const Qso& qso, QWidget* parent)
    : QDialog(parent), m_model(model), m_qso(qso)
{
    setWindowTitle(qso.id < 0 ? "New QSO" : "Edit QSO");
    setMinimumWidth(620);
    buildUI();
    populate();
}

void EditDialog::buildUI()
{
    auto* tabs = new QTabWidget;

    // ── Core ────────────────────────────────────────────────────────────
    auto* core = new QWidget;
    auto* coreL = new QFormLayout(core);
    m_call    = new QLineEdit;
    m_qsoDate = new QLineEdit;   m_qsoDate->setPlaceholderText("YYYYMMDD UTC");
    m_timeOn  = new QLineEdit;   m_timeOn->setPlaceholderText("HHMMSS UTC");
    m_timeOff = new QLineEdit;
    m_band    = new QLineEdit;
    m_freq    = new QLineEdit;   m_freq->setPlaceholderText("MHz, e.g. 14.250");
    m_mode    = new QLineEdit;
    m_submode = new QLineEdit;
    m_rstSent = new QLineEdit;
    m_rstRcvd = new QLineEdit;
    coreL->addRow("Call",        m_call);
    coreL->addRow("Date (UTC)",  m_qsoDate);
    coreL->addRow("Time on",     m_timeOn);
    coreL->addRow("Time off",    m_timeOff);
    coreL->addRow("Band",        m_band);
    coreL->addRow("Freq (MHz)",  m_freq);
    coreL->addRow("Mode",        m_mode);
    coreL->addRow("Submode",     m_submode);
    coreL->addRow("RST sent",    m_rstSent);
    coreL->addRow("RST rcvd",    m_rstRcvd);
    tabs->addTab(core, "Core");

    // ── Other Stn ───────────────────────────────────────────────────────
    auto* other = new QWidget;
    auto* otherL = new QFormLayout(other);
    m_name    = new QLineEdit;
    m_qth     = new QLineEdit;
    m_grid    = new QLineEdit;
    m_country = new QLineEdit;
    m_state   = new QLineEdit;
    m_cont    = new QLineEdit;
    m_dxcc    = new QSpinBox; m_dxcc->setMaximum(999);
    m_cqz     = new QSpinBox; m_cqz->setMaximum(40);
    m_ituz    = new QSpinBox; m_ituz->setMaximum(90);
    otherL->addRow("Name",      m_name);
    otherL->addRow("QTH",       m_qth);
    otherL->addRow("Grid",      m_grid);
    otherL->addRow("Country",   m_country);
    otherL->addRow("State",     m_state);
    otherL->addRow("Continent", m_cont);
    otherL->addRow("DXCC",      m_dxcc);
    otherL->addRow("CQ Zone",   m_cqz);
    otherL->addRow("ITU Zone",  m_ituz);
    tabs->addTab(other, "Other Stn");

    // ── My Stn ──────────────────────────────────────────────────────────
    auto* mine = new QWidget;
    auto* mineL = new QFormLayout(mine);
    m_myCall  = new QLineEdit;
    m_myGrid  = new QLineEdit;
    m_myState = new QLineEdit;
    m_txPwr   = new QDoubleSpinBox;
    m_txPwr->setRange(0.0, 99999.0);
    m_txPwr->setDecimals(1);
    m_txPwr->setSuffix(" W");
    mineL->addRow("My call",     m_myCall);
    mineL->addRow("My grid",     m_myGrid);
    mineL->addRow("My state",    m_myState);
    mineL->addRow("TX power",    m_txPwr);
    tabs->addTab(mine, "My Stn");

    // ── Contest ─────────────────────────────────────────────────────────
    auto* ctst = new QWidget;
    auto* ctstL = new QFormLayout(ctst);
    m_contestId = new QLineEdit;
    m_stx       = new QSpinBox; m_stx->setMaximum(999999);
    m_srx       = new QSpinBox; m_srx->setMaximum(999999);
    m_stxString = new QLineEdit;
    m_srxString = new QLineEdit;
    ctstL->addRow("Contest ID",  m_contestId);
    ctstL->addRow("STX serial",  m_stx);
    ctstL->addRow("SRX serial",  m_srx);
    ctstL->addRow("STX exch",    m_stxString);
    ctstL->addRow("SRX exch",    m_srxString);
    tabs->addTab(ctst, "Contest");

    // ── Notes / QSL ─────────────────────────────────────────────────────
    auto* notes = new QWidget;
    auto* notesL = new QFormLayout(notes);
    m_comment = new QPlainTextEdit; m_comment->setMaximumHeight(80);
    m_notes   = new QPlainTextEdit; m_notes->setMaximumHeight(80);
    auto buildQslCombo = []() {
        auto* c = new QComboBox; c->addItems(kQslStates); return c;
    };
    m_qslSent  = buildQslCombo();
    m_qslRcvd  = buildQslCombo();
    m_lotwSent = buildQslCombo();
    m_lotwRcvd = buildQslCombo();
    notesL->addRow("Comment",   m_comment);
    notesL->addRow("Notes",     m_notes);
    notesL->addRow("QSL sent",  m_qslSent);
    notesL->addRow("QSL rcvd",  m_qslRcvd);
    notesL->addRow("LoTW sent", m_lotwSent);
    notesL->addRow("LoTW rcvd", m_lotwRcvd);
    tabs->addTab(notes, "Notes / QSL");

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &EditDialog::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* main = new QVBoxLayout(this);
    main->addWidget(tabs);
    main->addWidget(btns);
}

void EditDialog::populate()
{
    m_call->setText(m_qso.call);
    m_qsoDate->setText(m_qso.qsoDate);
    m_timeOn->setText(m_qso.timeOn);
    m_timeOff->setText(m_qso.timeOff);
    m_band->setText(m_qso.band);
    m_freq->setText(m_qso.freq > 0.0 ? QString::number(m_qso.freq, 'f', 6) : QString{});
    m_mode->setText(m_qso.mode);
    m_submode->setText(m_qso.submode);
    m_rstSent->setText(m_qso.rstSent);
    m_rstRcvd->setText(m_qso.rstRcvd);

    m_name->setText(m_qso.name);
    m_qth->setText(m_qso.qth);
    m_grid->setText(m_qso.gridsquare);
    m_country->setText(m_qso.country);
    m_state->setText(m_qso.state);
    m_cont->setText(m_qso.cont);
    m_dxcc->setValue(m_qso.dxcc);
    m_cqz->setValue(m_qso.cqz);
    m_ituz->setValue(m_qso.ituz);

    m_myCall->setText(m_qso.myCall);
    m_myGrid->setText(m_qso.myGridsquare);
    m_myState->setText(m_qso.myState);
    m_txPwr->setValue(m_qso.txPwr);

    m_contestId->setText(m_qso.contestId);
    m_stx->setValue(m_qso.stx);
    m_srx->setValue(m_qso.srx);
    m_stxString->setText(m_qso.stxString);
    m_srxString->setText(m_qso.srxString);

    m_comment->setPlainText(m_qso.comment);
    m_notes->setPlainText(m_qso.notes);

    auto setCombo = [](QComboBox* c, const QString& v) {
        const int i = c->findText(v);
        c->setCurrentIndex(i >= 0 ? i : 0);
    };
    setCombo(m_qslSent,  m_qso.qslSent);
    setCombo(m_qslRcvd,  m_qso.qslRcvd);
    setCombo(m_lotwSent, m_qso.lotwSent);
    setCombo(m_lotwRcvd, m_qso.lotwRcvd);
}

void EditDialog::onAccept()
{
    m_qso.call         = m_call->text().trimmed().toUpper();
    m_qso.qsoDate      = m_qsoDate->text().trimmed();
    m_qso.timeOn       = m_timeOn->text().trimmed();
    m_qso.timeOff      = m_timeOff->text().trimmed();
    m_qso.band         = m_band->text().trimmed();
    m_qso.freq         = m_freq->text().trimmed().toDouble();
    m_qso.mode         = m_mode->text().trimmed().toUpper();
    m_qso.submode      = m_submode->text().trimmed().toUpper();
    m_qso.rstSent      = m_rstSent->text().trimmed();
    m_qso.rstRcvd      = m_rstRcvd->text().trimmed();
    m_qso.name         = m_name->text().trimmed();
    m_qso.qth          = m_qth->text().trimmed();
    m_qso.gridsquare   = m_grid->text().trimmed();
    m_qso.country      = m_country->text().trimmed();
    m_qso.state        = m_state->text().trimmed().toUpper();
    m_qso.cont         = m_cont->text().trimmed().toUpper();
    m_qso.dxcc         = m_dxcc->value();
    m_qso.cqz          = m_cqz->value();
    m_qso.ituz         = m_ituz->value();
    m_qso.myCall       = m_myCall->text().trimmed().toUpper();
    m_qso.myGridsquare = m_myGrid->text().trimmed();
    m_qso.myState      = m_myState->text().trimmed().toUpper();
    m_qso.txPwr        = m_txPwr->value();
    m_qso.contestId    = m_contestId->text().trimmed().toUpper();
    m_qso.stx          = m_stx->value();
    m_qso.srx          = m_srx->value();
    m_qso.stxString    = m_stxString->text().trimmed();
    m_qso.srxString    = m_srxString->text().trimmed();
    m_qso.comment      = m_comment->toPlainText().trimmed();
    m_qso.notes        = m_notes->toPlainText().trimmed();
    m_qso.qslSent      = m_qslSent->currentText();
    m_qso.qslRcvd      = m_qslRcvd->currentText();
    m_qso.lotwSent     = m_lotwSent->currentText();
    m_qso.lotwRcvd     = m_lotwRcvd->currentText();

    if (m_qso.call.isEmpty()) {
        QMessageBox::warning(this, "Invalid QSO", "Callsign is required.");
        return;
    }
    if (m_qso.qsoDate.isEmpty() || m_qso.timeOn.isEmpty()) {
        QMessageBox::warning(this, "Invalid QSO", "Date and time on are required.");
        return;
    }

    bool ok = false;
    if (m_qso.id < 0) ok = m_model->insertQso(m_qso);
    else              ok = m_model->updateQso(m_qso);
    if (!ok) {
        QMessageBox::critical(this, "Save failed", m_model->errorString());
        return;
    }
    accept();
}

} // namespace ShackBook
