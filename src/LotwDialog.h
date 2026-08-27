#pragma once

// LotwDialog — ARRL Logbook of the World: sign & upload via TQSL, and
// fetch confirmations back into the log.
//
// Upload half: exports the selected QSOs (default: everything not yet
// uploaded) to a temp ADIF, hands it to the user's installed `tqsl` for
// signing and upload (tqsl owns the callsign certificate — ShackBook never
// touches key material), and on success marks those QSOs lotw_sent='Y'
// with today's date. tqsl not installed / no certificate yet is a guided
// state, not an error: the dialog explains the one-time setup and links
// the ARRL download page, so a brand-new user can get from zero to
// uploaded without reading a manual.
//
// Confirmation half: pulls lotwreport.adi over HTTPS with the user's LoTW
// web credentials (NOT the certificate passphrase), matches each returned
// QSL against the log tolerantly (call+band+date, time to the minute) and
// sets lotw_rcvd/lotw_rdate — which the Awards panel already counts as
// "confirmed".

#include <QDialog>

class QDateEdit;
class QGroupBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QRadioButton;

namespace ShackBook {

class LogbookModel;

class LotwDialog : public QDialog {
    Q_OBJECT

public:
    explicit LotwDialog(LogbookModel* model, QWidget* parent = nullptr);

private slots:
    void onBrowseTqsl();
    void onSignUpload();
    void onFetchConfirmations();

private:
    QString tqslPath() const;          // configured, else auto-detected
    static QString autoDetectTqsl();
    void refreshGuidance();            // top banner + button enable states
    void refreshUnsentCount();
    void appendLog(const QString& line);
    void persistSettings();

    LogbookModel* m_model{};
    QNetworkAccessManager* m_nam{};

    QLabel*       m_guidance{};
    QLineEdit*    m_tqslPath{};
    QLineEdit*    m_location{};
    QRadioButton* m_rangeUnsent{};
    QRadioButton* m_rangeDates{};
    QDateEdit*    m_dateFrom{};
    QDateEdit*    m_dateTo{};
    QPushButton*  m_btnUpload{};

    QLineEdit*    m_user{};
    QLineEdit*    m_pass{};
    QDateEdit*    m_since{};
    QPushButton*  m_btnFetch{};

    QPlainTextEdit* m_log{};
};

} // namespace ShackBook
