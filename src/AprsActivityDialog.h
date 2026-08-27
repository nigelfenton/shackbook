#pragma once

// AprsActivityDialog — "who's on APRS right now" window. Connects to
// AetherSDR's KISS TNC (default localhost:8001), decodes the off-air AX.25
// traffic, and shows a live roster of heard stations sorted by how recently
// they were heard, with great-circle distance/bearing from MY_GRIDSQUARE and
// a ✓ against any call already in the log.
//
// A message row at the bottom lets you send an APRS text message back out
// through AetherSDR (the seed of the Stage-3 mailbox). Everything is driven
// by AprsKissClient + AprsStationModel; this class is just the wiring + UI.

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QTimer;
class QSortFilterProxyModel;

namespace ShackBook {

class LogbookModel;
class AprsKissClient;
class AprsStationModel;

class AprsActivityDialog : public QDialog {
    Q_OBJECT

public:
    explicit AprsActivityDialog(LogbookModel* model, QWidget* parent = nullptr);
    ~AprsActivityDialog() override;

private slots:
    void onConnectClicked();
    void onConnectionChanged(bool connected);
    void onSendClicked();
    void onRosterChanged(int count);
    void onPruneTick();

private:
    void refreshWorkedCalls();
    void setStatus(const QString& text, bool ok);

    LogbookModel*     m_model{nullptr};
    AprsKissClient*   m_client{nullptr};
    AprsStationModel* m_stations{nullptr};
    QSortFilterProxyModel* m_proxy{nullptr};
    QTimer*           m_pruneTimer{nullptr};

    QLineEdit*   m_hostEdit{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QLabel*      m_statusLabel{nullptr};
    QLabel*      m_countLabel{nullptr};
    QTableView*  m_table{nullptr};

    QLineEdit*   m_toEdit{nullptr};
    QLineEdit*   m_msgEdit{nullptr};
    QPushButton* m_sendBtn{nullptr};
};

} // namespace ShackBook
