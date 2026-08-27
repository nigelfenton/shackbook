#pragma once

// EditDialog — modal QSO editor used for both new-QSO and edit-existing-QSO
// flows.  All ADIF-relevant fields are exposed across five tabs (Core,
// Other Stn, My Stn, Contest, Notes / QSL).  On Save, the dialog persists
// directly to the LogbookModel passed in by the caller.

#include "Qso.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QDoubleSpinBox;

namespace ShackBook {

class LogbookModel;

class EditDialog : public QDialog {
    Q_OBJECT
public:
    EditDialog(LogbookModel* model, const Qso& qso, QWidget* parent = nullptr);
    Qso editedQso() const { return m_qso; }

private slots:
    void onAccept();

private:
    void buildUI();
    void populate();

    LogbookModel* m_model;
    Qso           m_qso;

    QLineEdit*  m_call{};
    QLineEdit*  m_qsoDate{};
    QLineEdit*  m_timeOn{};
    QLineEdit*  m_timeOff{};
    QLineEdit*  m_band{};
    QLineEdit*  m_freq{};
    QLineEdit*  m_mode{};
    QLineEdit*  m_submode{};
    QLineEdit*  m_rstSent{};
    QLineEdit*  m_rstRcvd{};
    QLineEdit*  m_name{};
    QLineEdit*  m_qth{};
    QLineEdit*  m_grid{};
    QLineEdit*  m_country{};
    QLineEdit*  m_state{};
    QLineEdit*  m_cont{};
    QSpinBox*   m_dxcc{};
    QSpinBox*   m_cqz{};
    QSpinBox*   m_ituz{};
    QDoubleSpinBox* m_txPwr{};
    QLineEdit*  m_myCall{};
    QLineEdit*  m_myGrid{};
    QLineEdit*  m_myState{};
    QLineEdit*  m_contestId{};
    QSpinBox*   m_stx{};
    QSpinBox*   m_srx{};
    QLineEdit*  m_stxString{};
    QLineEdit*  m_srxString{};
    QPlainTextEdit* m_comment{};
    QPlainTextEdit* m_notes{};
    QComboBox*  m_qslSent{};
    QComboBox*  m_qslRcvd{};
    QComboBox*  m_lotwSent{};
    QComboBox*  m_lotwRcvd{};
};

} // namespace ShackBook
