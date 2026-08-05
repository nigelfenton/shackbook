#pragma once

// MainWindow — top-level ShackLog UI.
//
// Layout (top → bottom):
//   • MenuBar    — File / Edit / Tools / Help
//   • Header     — operator callsign | current freq / band / mode | TCI dot
//   • QuickEntry — CALL / RST sent / RST rcvd / comment / [SAVE] + DUPE badge
//                  (with optional contest sub-row when contestMode is on)
//   • Filter     — text / band / mode / contest / Reset
//   • Table      — QSO table, newest first
//   • Actions    — count label | [New] [Edit] [Delete]
//   • StatusBar  — TCI server endpoint | DB path
//
// Owns the LogbookModel and a TciClient singleton.

#include "CtyDat.h"
#include "Qso.h"

#include <QMainWindow>

class QAction;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace ShackLog {

class LogbookModel;
class TciClient;
class RigctldClient;
class SpotIndex;
class DxClusterClient;
class PotaClient;
class CallsignLookup;
class SectionMapDialog;
class GridMapDialog;
class AprsActivityDialog;
struct SpotData;
namespace Server { class WsjtxAdifReceiver; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    // Quick-entry
    void onCallEdited(const QString& text);
    void onSaveQso();

    // Table / filter
    void onFilterChanged();
    void onResetFilter();
    void onLogbookChanged();
    void onContextSettingsChanged();

    // Actions
    void onNewQso();
    void onEditQso();
    void onDeleteQso();
    void onSettings();
    void onSwitchLog();
    void onShowAwards();
    void onShowSectionMap();
    void onShowGridMap();
    void onShowAprsActivity();
    void onHowFar();
    void onImportAdif();
    void onExportAdif();
    void onExportCabrillo();
    void onAbout();

    // TCI
    void onConnectTci();
    // Scan for TCI servers and offer what answered a real handshake.
    void onFindRadios();
    // Which kind of radio link is in use: TCI (AetherSDR/ExpertSDR/SunSDR) or
    // Hamlib rigctld (everything else). Stored per log as RADIO_SOURCE.
    bool usingRigctld() const;
    // Connect whichever source RADIO_SOURCE selects.
    void connectActiveSource();
    void onDisconnectTci();
    void onTciConnectionChanged(bool connected);
    void onTciFrequencyChanged(double mhz);
    void onTciModeChanged(const QString& mode);

    // DX cluster (Phase 2)
    void onClusterConnectionChanged(bool connected);
    void onClusterSpotReceived(const ShackLog::SpotData& spot);
    void onClusterRawLine(const QString& line);
    void purgeStaleSpots();
    void onShowClusterLog();
    void onShowSpotIndex();
    void onShowLotw();

    // POTA (Phase 3)
    void onPotaSpotReceived(const ShackLog::SpotData& spot);
    void onPotaPollCompleted(int spots, const QString& errorOrEmpty);

private:
    void buildUI();
    void buildMenus();
    void refreshHeader();
    void refreshQuickEntry();
    void refreshContestUI();
    void refreshTable();
    // Operator chooser: pick / create the per-callsign log and (re)open it.
    // At startup a cancel falls back to the last-used (or legacy) log; on a
    // live switch a cancel keeps the current log open.
    bool chooseAndOpenLog(bool startup);
    void populateBandFilter();
    void populateModeFilter();
    void populateContestFilter();
    void refreshDupBadge();
    void refreshStatusBar();
    // "ShackLog — G0JKN — 0.5.0-dev". Carries the version always, and the
    // -dev tag on any build that is not an installed release.
    static QString windowTitleFor(const QString& operatorCall);
    // The radio to stamp on a QSO, resolved as: per-connection nickname →
    // the TCI-announced device name → empty. Empty when not connected, so an
    // offline QSO is left unattributed rather than credited to the last radio.
    QString currentRadioName() const;
    void applyAutoConnectFromSettings();
    void applyClusterConfigFromSettings();
    void applyPotaConfigFromSettings();
    void applyLookupConfigFromSettings();
    void tryAutofillFromSpot();
    // Kick the callsign-lookup chain (worked-before → cty.dat → online).
    // Results land in m_lookupFill and merge into empty fields at save.
    void startCallsignLookup(const QString& call);
    qint64 selectedQsoId() const;

    LogbookModel*    m_model{nullptr};
    TciClient*       m_tci{nullptr};
    // The non-TCI path: Hamlib rigctld, for Icom/Yaesu/Kenwood and anything
    // else Hamlib drives. Exactly one of the two is connected at a time —
    // both following the same radio would fight over the CAT port.
    RigctldClient*   m_rigctld{nullptr};
    SpotIndex*       m_spotIndex{nullptr};
    DxClusterClient* m_dxc{nullptr};
    PotaClient*      m_pota{nullptr};
    QTimer*          m_spotPurgeTimer{nullptr};
    QPlainTextEdit*  m_dxcLog{nullptr};   // diagnostic — see onShowClusterLog()

    // Cached TCI state
    double  m_curFreqMhz{0.0};
    QString m_curBand;
    QString m_curMode;       // ADIF base mode
    QString m_curSubmode;    // ADIF submode (USB/LSB)
    QString m_rawTciMode;    // for display only

    // Last call we auto-filled — used to decide whether the call field
    // is "ours" (safe to overwrite on the next spot click) or the
    // operator's typed input (must be left alone).
    QString m_lastAutofilledCall;
    QString m_lastAutofilledComment;

    // Callsign lookup chain
    CtyDat           m_cty;              // offline prefix → country/zones
    CallsignLookup*  m_lookup{nullptr};  // online QRZ / HamQTH / callook
    QTimer*          m_lookupDebounce{nullptr};  // typed-call settle timer
    SectionMapDialog* m_sectionMap{nullptr};     // non-modal, lazily created
    GridMapDialog*    m_gridMap{nullptr};        // non-modal, lazily created
    AprsActivityDialog* m_aprsActivity{nullptr}; // non-modal, lazily created
    Qso     m_lookupFill;        // pending details for the next save…
    QString m_lookupFillCall;    // …valid only while the call field matches
    QString m_operatorCall;          // whose log is open (multi-log)
    Server::WsjtxAdifReceiver* m_wsjtx{};  // WSJT-X UDP/ADIF → active log

    // Header
    QLabel*  m_myCallLabel{};
    QLabel*  m_freqLabel{};
    QLabel*  m_bandLabel{};
    QLabel*  m_modeLabel{};
    QLabel*  m_tciDot{};
    QLabel*  m_tciStatus{};

    // Quick entry
    QLineEdit*   m_callEdit{};
    QLineEdit*   m_rstSentEdit{};
    QLineEdit*   m_rstRcvdEdit{};
    QLineEdit*   m_commentEdit{};
    QPushButton* m_saveBtn{};
    QLabel*      m_dupBadge{};

    // Contest sub-row
    QFrame*    m_contestFrame{};
    QLabel*    m_contestIdLabel{};
    QLineEdit* m_stxEdit{};
    QLineEdit* m_stxStringEdit{};
    QLineEdit* m_srxEdit{};
    QLineEdit* m_srxStringEdit{};

    // Filter row
    QLineEdit*   m_filterText{};
    QComboBox*   m_filterBand{};
    QComboBox*   m_filterMode{};
    QComboBox*   m_filterContest{};
    QPushButton* m_resetBtn{};

    // Table + actions
    QTableWidget* m_table{};
    QLabel*       m_countLabel{};
    QPushButton*  m_howFarBtn{};
    QPushButton*  m_newBtn{};
    QPushButton*  m_editBtn{};
    QPushButton*  m_deleteBtn{};

    // Status bar permanent widgets
    QLabel* m_sbTci{};
    QLabel* m_sbDxc{};
    QLabel* m_sbDb{};

    // Menu actions
    QAction* m_actSwitchLog{};
    QAction* m_actAwards{};
    QAction* m_actSectionMap{};
    QAction* m_actAprsActivity{};
    QAction* m_actHowFarMap{};
    QAction* m_actImportAdif{};
    QAction* m_actExportAdif{};
    QAction* m_actExportCab{};
    QAction* m_actSettings{};
    QAction* m_actFindRadios{};
    QAction* m_actConnectTci{};
    QAction* m_actDisconnectTci{};
    QAction* m_actDxcLog{};
    QAction* m_actSpotIndex{};
    QAction* m_actLotw{};
    QAction* m_actNew{};
    QAction* m_actEdit{};
    QAction* m_actDelete{};
    QAction* m_actAbout{};
    QAction* m_actQuit{};
};

} // namespace ShackLog
