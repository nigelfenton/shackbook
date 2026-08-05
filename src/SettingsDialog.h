#pragma once

// SettingsDialog — operator / contest / Cabrillo / TCI server settings.
//
// Lives in tabs:
//   • Operator — MY_CALL, MY_GRIDSQUARE, MY_STATE, default TX power, OPERATOR
//   • TCI      — host, port, autoconnect on launch
//   • Lookup   — callsign → name/QTH lookup chain (worked-before, cty.dat,
//                QRZ XML / HamQTH / callook.info online providers)
//   • Contest  — contest mode toggle, contest id, next STX serial
//   • Cabrillo — header fields used by Cabrillo export
//
// All values are persisted to the LogbookModel's settings table on Save.

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;

namespace ShackLog {

class LogbookModel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(LogbookModel* model, QWidget* parent = nullptr);

private slots:
    void onAccept();
    // Scan for TCI servers and fill in the host/port of whichever is chosen.
    void onScanForRadios();

private:
    void buildUI();
    void populate();

    LogbookModel* m_model;

    // Operator
    QLineEdit*  m_myCall{};
    QLineEdit*  m_myGrid{};
    QLineEdit*  m_myState{};
    QDoubleSpinBox* m_defaultTxPwr{};
    QLineEdit*  m_myOperator{};

    // TCI
    QLineEdit*  m_tciHost{};
    QSpinBox*   m_tciPort{};
    QComboBox*   m_radioSource{};
    QPushButton* m_tciScan{};
    QLineEdit*  m_tciNickname{};
    QCheckBox*  m_tciAutoConnect{};

    // DX Cluster
    QCheckBox*  m_dxcEnable{};
    QCheckBox*  m_dxcAutoDetect{};
    QLineEdit*  m_dxcHost{};
    QSpinBox*   m_dxcPort{};
    QLineEdit*  m_dxcCallsign{};
    QComboBox*  m_dxcLoginSuffix{};
    QLabel*     m_dxcDetected{};

    // POTA (HTTP poll, independent of cluster)
    QCheckBox*  m_potaEnable{};
    QSpinBox*   m_potaPollSec{};

    // Callsign lookup
    QCheckBox*  m_lkWorkedBefore{};
    QCheckBox*  m_lkCty{};
    QComboBox*  m_lkProvider{};
    QLineEdit*  m_lkUser{};
    QLineEdit*  m_lkPass{};
    QCheckBox*  m_lkCallook{};

    // Contest
    QCheckBox*  m_contestMode{};
    QComboBox*  m_contestId{};
    QSpinBox*   m_stxNext{};

    // Cabrillo
    QLineEdit*  m_cbName{};
    QLineEdit*  m_cbAddress{};
    QLineEdit*  m_cbEmail{};
    QLineEdit*  m_cbClub{};
    QLineEdit*  m_cbLocation{};
    QComboBox*  m_cbCatOp{};
    QComboBox*  m_cbCatAssisted{};
    QComboBox*  m_cbCatBand{};
    QComboBox*  m_cbCatMode{};
    QComboBox*  m_cbCatPower{};
    QComboBox*  m_cbCatStation{};
    QComboBox*  m_cbCatTransmitter{};
};

} // namespace ShackLog
