#include "SettingsDialog.h"

#include "LogbookModel.h"
#include "AetherSettingsReader.h"
#include "TciClient.h"      // tciNicknameKey()
#include "RigctldClient.h"   // rigctldNicknameKey()
#include "TciDiscovery.h"

#include <QComboBox>
#include <QEventLoop>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QTabWidget>

namespace ShackLog {

namespace {
const QStringList kContestIds = {
    "",
    "CQ-WW-CW", "CQ-WW-SSB", "CQ-WW-RTTY",
    "CQ-WPX-CW", "CQ-WPX-SSB", "CQ-WPX-RTTY",
    "ARRL-DX-CW", "ARRL-DX-SSB",
    "ARRL-FD",
    "ARRL-SS-CW", "ARRL-SS-SSB",
    "ARRL-10", "ARRL-160",
    "IARU-HF",
    "WAEDC-CW", "WAEDC-SSB",
    "RDXC", "IOTA",
    "BARTG-RTTY",
    "NAQP-CW", "NAQP-SSB",
};

const QStringList kCatOp      = { "SINGLE-OP", "MULTI-OP", "CHECKLOG" };
const QStringList kCatAssist  = { "NON-ASSISTED", "ASSISTED" };
const QStringList kCatBand    = { "ALL", "160M", "80M", "40M", "20M", "15M", "10M", "6M", "2M" };
const QStringList kCatMode    = { "MIXED", "CW", "SSB", "RTTY", "DIGI", "FM" };
const QStringList kCatPower   = { "HIGH", "LOW", "QRP" };
const QStringList kCatStation = { "FIXED", "MOBILE", "PORTABLE", "ROVER", "HQ", "SCHOOL" };
const QStringList kCatTx      = { "ONE", "TWO", "LIMITED", "UNLIMITED", "SWL" };
} // namespace

SettingsDialog::SettingsDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle("ShackLog Settings");
    setMinimumWidth(560);
    buildUI();
    populate();
}

void SettingsDialog::buildUI()
{
    auto* tabs = new QTabWidget;

    // ── Operator ────────────────────────────────────────────────────────
    auto* op = new QWidget;
    auto* opL = new QFormLayout(op);
    m_myCall       = new QLineEdit;
    m_myGrid       = new QLineEdit;
    m_myState      = new QLineEdit;
    m_defaultTxPwr = new QDoubleSpinBox;
    m_defaultTxPwr->setRange(0.0, 99999.0);
    m_defaultTxPwr->setDecimals(1);
    m_defaultTxPwr->setSuffix(" W");
    m_myOperator   = new QLineEdit;
    opL->addRow("My call",       m_myCall);
    opL->addRow("My grid",       m_myGrid);
    opL->addRow("My state",      m_myState);
    opL->addRow("Default power", m_defaultTxPwr);
    opL->addRow("Operator",      m_myOperator);
    tabs->addTab(op, "Operator");

    // ── TCI ─────────────────────────────────────────────────────────────
    auto* tci = new QWidget;
    auto* tciL = new QFormLayout(tci);
    m_tciHost = new QLineEdit;
    m_tciHost->setPlaceholderText("127.0.0.1");
    m_tciPort = new QSpinBox;
    m_tciPort->setRange(1, 65535);
    m_tciNickname = new QLineEdit;
    m_tciNickname->setPlaceholderText("e.g. Hermes-Lite 2");
    m_tciNickname->setToolTip(
        "Name recorded on each QSO made through this server.\n\n"
        "TCI reports the application (\"AetherSDR\"), not the radio, so two "
        "rigs driven by the same software look identical without a nickname.\n\n"
        "Stored per host:port. Leave blank to record the announced name.");
    m_tciAutoConnect = new QCheckBox("Connect to TCI server on launch");
    m_radioSource = new QComboBox;
    m_radioSource->addItem("TCI — AetherSDR, ExpertSDR, SunSDR", "tci");
    m_radioSource->addItem("Hamlib rigctld — Icom, Yaesu, Kenwood, others", "rigctld");
    m_radioSource->setToolTip(
        "How ShackLog follows your radio.\n\n"
        "TCI is built into AetherSDR and ExpertSDR — nothing else to install.\n\n"
        "rigctld covers everything else, but needs Hamlib installed and running "
        "(ShackLog does not include it). Its advantage: it reports the RADIO's "
        "model, so contacts are attributed correctly without a nickname.");
    tciL->addRow("Follow radio via", m_radioSource);

    // ShackLog does not ship Hamlib, so say plainly whether it is here. Without
    // this the operator gets "CAT: ✗" and no way to tell "not installed" from
    // "radio switched off" — different problems with different fixes.
    m_hamlibStatus = new QLabel;
    m_hamlibStatus->setWordWrap(true);
    m_hamlibStatus->setOpenExternalLinks(true);
    m_hamlibStatus->setTextFormat(Qt::RichText);
    tciL->addRow(QString(), m_hamlibStatus);

    m_rigctldPath = new QLineEdit;
    m_rigctldPath->setPlaceholderText("auto-detect");
    m_rigctldPath->setToolTip(
        "Where rigctld lives, if ShackLog cannot find it on its own.\n\n"
        "Leave blank to look in the usual places and on PATH.");
    tciL->addRow("rigctld program", m_rigctldPath);

    m_tciScan = new QPushButton("Find radios…");
    m_tciScan->setToolTip(
        "Look for TCI servers on this machine and the host above.\n\n"
        "Only servers that answer a real TCI handshake are offered — a port "
        "that merely accepts a connection is not a radio.");
    connect(m_tciScan, &QPushButton::clicked, this, &SettingsDialog::onScanForRadios);
    tciL->addRow("Host", m_tciHost);
    tciL->addRow("Port", m_tciPort);
    tciL->addRow(QString(), m_tciScan);
    tciL->addRow("Radio nickname", m_tciNickname);
    tciL->addRow(m_tciAutoConnect);
    tabs->addTab(tci, "TCI");

    // ── DX Cluster ──────────────────────────────────────────────────────
    auto* dxc = new QWidget;
    auto* dxcL = new QFormLayout(dxc);
    m_dxcEnable     = new QCheckBox("Enable DX cluster spotting (auto-fill CALL on QSY)");
    m_dxcAutoDetect = new QCheckBox("Auto-detect cluster from AetherSDR's settings file");
    m_dxcHost       = new QLineEdit;
    m_dxcHost->setPlaceholderText("dxc.nc7j.com");
    m_dxcPort       = new QSpinBox;
    m_dxcPort->setRange(1, 65535);
    m_dxcPort->setValue(7300);
    m_dxcCallsign   = new QLineEdit;
    m_dxcCallsign->setPlaceholderText("G0JKN");
    m_dxcLoginSuffix = new QComboBox;
    m_dxcLoginSuffix->setEditable(true);
    m_dxcLoginSuffix->addItem("-2",  "-2");      // DXSpider preferred
    m_dxcLoginSuffix->addItem("-1",  "-1");
    m_dxcLoginSuffix->addItem("-3",  "-3");
    m_dxcLoginSuffix->addItem("-L (CC Cluster / AR-Cluster)", "-L");
    m_dxcLoginSuffix->addItem("(none — bare callsign)",       "");
    m_dxcDetected   = new QLabel("(no AetherSDR config detected)");
    m_dxcDetected->setStyleSheet("QLabel { color: #6b8099; font-size: 10px; }");
    m_dxcDetected->setWordWrap(true);

    dxcL->addRow(m_dxcEnable);
    dxcL->addRow(m_dxcAutoDetect);
    dxcL->addRow("Detected", m_dxcDetected);
    dxcL->addRow("Host (override)",     m_dxcHost);
    dxcL->addRow("Port (override)",     m_dxcPort);
    dxcL->addRow("Callsign (override)", m_dxcCallsign);
    dxcL->addRow("Login suffix",        m_dxcLoginSuffix);

    // ── POTA section (lives on the same tab — both feed the SpotIndex) ──
    auto* potaSep = new QLabel("─── POTA (api.pota.app) ───");
    potaSep->setStyleSheet("QLabel { color: #6b8099; font-size: 9px; "
                           "font-weight: bold; letter-spacing: 0.08em; }");
    dxcL->addRow(potaSep);

    m_potaEnable  = new QCheckBox("Enable POTA spotting (Parks On The Air HTTP feed)");
    m_potaPollSec = new QSpinBox;
    m_potaPollSec->setRange(5, 600);
    m_potaPollSec->setSuffix(" s");
    dxcL->addRow(m_potaEnable);
    dxcL->addRow("POTA poll interval", m_potaPollSec);

    auto refreshDxcEditable = [this]() {
        const bool manual = !m_dxcAutoDetect->isChecked();
        m_dxcHost->setEnabled(manual);
        m_dxcPort->setEnabled(manual);
        m_dxcCallsign->setEnabled(manual);
    };
    connect(m_dxcAutoDetect, &QCheckBox::toggled, this, [refreshDxcEditable](bool){
        refreshDxcEditable();
    });

    tabs->addTab(dxc, "DX Cluster");

    // ── Callsign lookup ─────────────────────────────────────────────────
    auto* lk = new QWidget;
    auto* lkL = new QFormLayout(lk);
    m_lkWorkedBefore = new QCheckBox("Fill name/QTH from previous QSOs with this call");
    m_lkCty          = new QCheckBox("Fill country/continent/zones from cty.dat (offline)");
    m_lkProvider     = new QComboBox;
    m_lkProvider->addItem("None",                                "none");
    m_lkProvider->addItem("QRZ.com (XML subscription required)", "qrz");
    m_lkProvider->addItem("HamQTH.com (free account)",           "hamqth");
    m_lkUser = new QLineEdit;
    m_lkPass = new QLineEdit;
    m_lkPass->setEchoMode(QLineEdit::Password);
    m_lkCallook = new QCheckBox(
        "Use callook.info for US calls when no provider is set (no account)");
    auto* lkNote = new QLabel(
        "Looked-up details only ever fill EMPTY fields — anything you type "
        "wins. QRZ XML lookups need a paid QRZ XML-data subscription; a free "
        "QRZ web login will return an error. The password is stored in this "
        "log's database file.");
    lkNote->setStyleSheet("QLabel { color: #6b8099; font-size: 10px; }");
    lkNote->setWordWrap(true);
    lkL->addRow(m_lkWorkedBefore);
    lkL->addRow(m_lkCty);
    lkL->addRow("Online provider", m_lkProvider);
    lkL->addRow("Username",        m_lkUser);
    lkL->addRow("Password",        m_lkPass);
    lkL->addRow(m_lkCallook);
    lkL->addRow(lkNote);

    auto refreshLkEditable = [this]() {
        const bool online =
            m_lkProvider->currentData().toString() != QLatin1String("none");
        m_lkUser->setEnabled(online);
        m_lkPass->setEnabled(online);
        m_lkCallook->setEnabled(!online);
    };
    connect(m_lkProvider, &QComboBox::currentIndexChanged, this,
            [refreshLkEditable](int){ refreshLkEditable(); });

    tabs->addTab(lk, "Lookup");

    // ── Contest ─────────────────────────────────────────────────────────
    auto* ctst = new QWidget;
    auto* ctstL = new QFormLayout(ctst);
    m_contestMode = new QCheckBox("Contest mode (show contest panel in main window)");
    m_contestId   = new QComboBox;
    m_contestId->setEditable(true);
    m_contestId->addItems(kContestIds);
    m_stxNext = new QSpinBox;
    m_stxNext->setRange(1, 999999);
    ctstL->addRow(m_contestMode);
    ctstL->addRow("Contest ID",      m_contestId);
    ctstL->addRow("Next STX serial", m_stxNext);
    tabs->addTab(ctst, "Contest");

    // ── Cabrillo ────────────────────────────────────────────────────────
    auto* cab = new QWidget;
    auto* cabL = new QFormLayout(cab);
    m_cbName     = new QLineEdit;
    m_cbAddress  = new QLineEdit;
    m_cbEmail    = new QLineEdit;
    m_cbClub     = new QLineEdit;
    m_cbLocation = new QLineEdit;
    auto buildCombo = [](const QStringList& items) {
        auto* c = new QComboBox; c->addItems(items); return c;
    };
    m_cbCatOp          = buildCombo(kCatOp);
    m_cbCatAssisted    = buildCombo(kCatAssist);
    m_cbCatBand        = buildCombo(kCatBand);
    m_cbCatMode        = buildCombo(kCatMode);
    m_cbCatPower       = buildCombo(kCatPower);
    m_cbCatStation     = buildCombo(kCatStation);
    m_cbCatTransmitter = buildCombo(kCatTx);
    cabL->addRow("Name",          m_cbName);
    cabL->addRow("Address",       m_cbAddress);
    cabL->addRow("Email",         m_cbEmail);
    cabL->addRow("Club",          m_cbClub);
    cabL->addRow("Location",      m_cbLocation);
    cabL->addRow("Operator cat",  m_cbCatOp);
    cabL->addRow("Assisted",      m_cbCatAssisted);
    cabL->addRow("Band cat",      m_cbCatBand);
    cabL->addRow("Mode cat",      m_cbCatMode);
    cabL->addRow("Power cat",     m_cbCatPower);
    cabL->addRow("Station cat",   m_cbCatStation);
    cabL->addRow("Transmitter",   m_cbCatTransmitter);
    tabs->addTab(cab, "Cabrillo");

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* main = new QVBoxLayout(this);
    main->addWidget(tabs);
    main->addWidget(btns);
}

void SettingsDialog::populate()
{
    m_myCall->setText(m_model->myCall());
    m_myGrid->setText(m_model->myGridsquare());
    m_myState->setText(m_model->myState());
    m_defaultTxPwr->setValue(m_model->defaultTxPwr());
    m_myOperator->setText(m_model->settingValue("MY_OPERATOR"));

    {
        const QString src = m_model->settingValue("RADIO_SOURCE", "tci");
        const int idx = m_radioSource->findData(src);
        m_radioSource->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    // Host and port belong to whichever source is selected, so switching the
    // combo swaps them rather than leaving a TCI port against a CAT radio.
    const auto loadEndpoint = [this]() {
        const bool rig = m_radioSource->currentData().toString() == QLatin1String("rigctld");
        m_tciHost->setText(m_model->settingValue(rig ? "RIGCTLD_HOST" : "TCI_HOST",
                                                 "127.0.0.1"));
        m_tciPort->setValue(m_model->settingValue(rig ? "RIGCTLD_PORT" : "TCI_PORT",
                                                  rig ? "4532" : "40001").toInt());
        const QString h = m_tciHost->text();
        const QString p = QString::number(m_tciPort->value());
        m_tciNickname->setText(m_model->settingValue(
            rig ? rigctldNicknameKey(h, p) : tciNicknameKey(h, p)));
    };
    m_rigctldPath->setText(m_model->settingValue("HAMLIB_RIGCTLD_PATH"));
    loadEndpoint();
    connect(m_radioSource, &QComboBox::currentIndexChanged, this,
            [this, loadEndpoint](int) {
                loadEndpoint();
                refreshHamlibGuidance();
            });
    connect(m_rigctldPath, &QLineEdit::textChanged, this,
            [this](const QString&) { refreshHamlibGuidance(); });
    refreshHamlibGuidance();
    m_tciAutoConnect->setChecked(m_model->settingValue("TCI_AUTOCONNECT", "1") == "1");

    // DX Cluster — auto-detect from AetherSDR's config, default to "on" if
    // a cluster is configured there; otherwise off until the user opts in.
    const auto aether = AetherSettingsReader::readDxClusterConfig();
    if (aether.found) {
        m_dxcDetected->setText(
            QString("AetherSDR: %1:%2 as %3 (login will be sent as %3-L)")
                .arg(aether.host).arg(aether.port).arg(aether.callsign));
    } else {
        m_dxcDetected->setText(
            "(AetherSDR config not found at " + AetherSettingsReader::defaultSettingsPath()
            + " — uncheck auto-detect to enter cluster details manually)");
    }
    const QString defEnable = aether.found ? "1" : "0";
    m_dxcEnable->setChecked(m_model->settingValue("DXC_ENABLE", defEnable) == "1");
    m_dxcAutoDetect->setChecked(m_model->settingValue("DXC_AUTODETECT", "1") == "1");
    m_dxcHost->setText(m_model->settingValue("DXC_HOST",
                            aether.found ? aether.host : QString("dxc.nc7j.com")));
    m_dxcPort->setValue(m_model->settingValue("DXC_PORT",
                            QString::number(aether.found ? aether.port : 7300)).toInt());
    m_dxcCallsign->setText(m_model->settingValue("DXC_CALLSIGN", aether.callsign));
    const QString storedSuffix = m_model->settingValue("DXC_LOGIN_SUFFIX", "-2");
    int suffixIdx = -1;
    for (int i = 0; i < m_dxcLoginSuffix->count(); ++i) {
        if (m_dxcLoginSuffix->itemData(i).toString() == storedSuffix) {
            suffixIdx = i;
            break;
        }
    }
    if (suffixIdx >= 0) m_dxcLoginSuffix->setCurrentIndex(suffixIdx);
    else                m_dxcLoginSuffix->setEditText(storedSuffix);

    m_potaEnable->setChecked(m_model->settingValue("POTA_ENABLE", "1") == "1");
    m_potaPollSec->setValue(m_model->settingValue("POTA_POLL_SEC", "30").toInt());

    const bool manual = !m_dxcAutoDetect->isChecked();
    m_dxcHost->setEnabled(manual);
    m_dxcPort->setEnabled(manual);
    m_dxcCallsign->setEnabled(manual);

    const QString lkProv = m_model->settingValue("LOOKUP_PROVIDER", "none");
    for (int i = 0; i < m_lkProvider->count(); ++i) {
        if (m_lkProvider->itemData(i).toString() == lkProv) {
            m_lkProvider->setCurrentIndex(i);
            break;
        }
    }
    m_lkWorkedBefore->setChecked(m_model->settingValue("LOOKUP_WORKEDBEFORE", "1") == "1");
    m_lkCty->setChecked(m_model->settingValue("LOOKUP_CTY", "1") == "1");
    m_lkUser->setText(m_model->settingValue("LOOKUP_USERNAME"));
    m_lkPass->setText(m_model->settingValue("LOOKUP_PASSWORD"));
    m_lkCallook->setChecked(m_model->settingValue("LOOKUP_CALLOOK", "1") == "1");
    {
        const bool online = lkProv != QLatin1String("none");
        m_lkUser->setEnabled(online);
        m_lkPass->setEnabled(online);
        m_lkCallook->setEnabled(!online);
    }

    m_contestMode->setChecked(m_model->contestMode());
    const QString cid = m_model->contestId();
    int idx = m_contestId->findText(cid);
    if (idx >= 0) m_contestId->setCurrentIndex(idx);
    else          m_contestId->setEditText(cid);
    m_stxNext->setValue(m_model->settingValue("CONTEST_STX_NEXT", "1").toInt());

    m_cbName->setText(m_model->settingValue("CABRILLO_NAME"));
    m_cbAddress->setText(m_model->settingValue("CABRILLO_ADDRESS"));
    m_cbEmail->setText(m_model->settingValue("CABRILLO_EMAIL"));
    m_cbClub->setText(m_model->settingValue("CABRILLO_CLUB"));
    m_cbLocation->setText(m_model->settingValue("CABRILLO_LOCATION"));
    auto setCombo = [](QComboBox* c, const QString& v) {
        const int i = c->findText(v);
        if (i >= 0) c->setCurrentIndex(i);
    };
    setCombo(m_cbCatOp,          m_model->settingValue("CABRILLO_CAT_OPERATOR",     "SINGLE-OP"));
    setCombo(m_cbCatAssisted,    m_model->settingValue("CABRILLO_CAT_ASSISTED",     "NON-ASSISTED"));
    setCombo(m_cbCatBand,        m_model->settingValue("CABRILLO_CAT_BAND",         "ALL"));
    setCombo(m_cbCatMode,        m_model->settingValue("CABRILLO_CAT_MODE",         "MIXED"));
    setCombo(m_cbCatPower,       m_model->settingValue("CABRILLO_CAT_POWER",        "HIGH"));
    setCombo(m_cbCatStation,     m_model->settingValue("CABRILLO_CAT_STATION",      "FIXED"));
    setCombo(m_cbCatTransmitter, m_model->settingValue("CABRILLO_CAT_TRANSMITTER",  "ONE"));
}

void SettingsDialog::refreshHamlibGuidance()
{
    if (!m_hamlibStatus) return;

    const bool rig = m_radioSource->currentData().toString() == QLatin1String("rigctld");
    // Only relevant when a CAT radio is selected. Saying nothing for TCI users
    // keeps the requirement from reading as a barrier to the whole app.
    m_hamlibStatus->setVisible(rig);
    m_rigctldPath->setVisible(rig);
    if (!rig) return;

    const QString found = RigctldClient::findRigctld(m_rigctldPath->text());

    if (found.isEmpty()) {
        m_hamlibStatus->setStyleSheet("QLabel { color: #d08a3e; font-size: 11px; }");
        m_hamlibStatus->setText(tr(
            "<b>Hamlib not found.</b> Following a non-TCI radio needs Hamlib's "
            "<code>rigctld</code>, which ShackLog does not include. "
            "<a href=\"https://hamlib.github.io/\">Get Hamlib</a> — or if you "
            "already run WSJT-X or fldigi you very likely have it, and can point "
            "at it above. ShackLog will not start it for you: run it yourself so "
            "nothing else loses the serial port."));
        return;
    }

    m_hamlibStatus->setStyleSheet("QLabel { color: #6b8099; font-size: 11px; }");
    m_hamlibStatus->setText(tr(
        "Hamlib found at <code>%1</code>. It still has to be RUNNING and "
        "connected to the radio — ShackLog only talks to it, and does not "
        "start it. For example:<br><code>rigctld -m &lt;model&gt; -r &lt;port&gt;</code>")
            .arg(found.toHtmlEscaped()));
}

void SettingsDialog::onScanForRadios()
{
    // Sweep loopback plus whatever host is TYPED in the box right now — the
    // operator may be pointing at a new machine and not have saved it yet.
    QStringList hosts{QStringLiteral("127.0.0.1")};
    const QString typed = m_tciHost->text().trimmed();
    if (!typed.isEmpty()) hosts << typed;

    QProgressDialog progress(tr("Looking for radios…"), tr("Cancel"), 0, 1, this);
    progress.setWindowTitle(tr("Find radios"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    TciDiscovery discovery;
    QList<TciServerInfo> found;

    connect(&discovery, &TciDiscovery::progress, &progress,
            [&progress](int done, int total) {
                progress.setMaximum(total);
                progress.setValue(done);
            });

    QEventLoop loop;
    connect(&discovery, &TciDiscovery::finished, &loop,
            [&](const QList<TciServerInfo>& servers) { found = servers; loop.quit(); });
    connect(&progress, &QProgressDialog::canceled, &discovery, &TciDiscovery::cancel);

    discovery.scan(hosts, TciDiscovery::defaultPorts());
    loop.exec();
    progress.close();

    if (found.isEmpty()) {
        QMessageBox::information(
            this, tr("Find radios"),
            tr("No TCI servers answered.\n\n"
               "A radio only appears here if its software is running and TCI is "
               "switched on — in AetherSDR that is Tools → TCI."));
        return;
    }

    QStringList labels;
    labels.reserve(found.size());
    for (const TciServerInfo& s : found) labels << s.label();

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Find radios"),
        tr("These answered a TCI handshake — pick one to use:"),
        labels, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    const int idx = labels.indexOf(chosen);
    if (idx < 0) return;
    const TciServerInfo& pick = found.at(idx);

    // Fill the fields; nothing is saved until the operator accepts the dialog.
    m_tciHost->setText(pick.host);
    m_tciPort->setValue(pick.port);

    // Offer the announced name as a starting nickname when none is set — it is
    // the application name, so it still needs editing when two radios share
    // one server, but it beats a blank box.
    if (m_tciNickname->text().trimmed().isEmpty() && !pick.device.isEmpty())
        m_tciNickname->setText(pick.device);
}

void SettingsDialog::onAccept()
{
    m_model->setSetting("MY_CALL",          m_myCall->text().trimmed().toUpper());
    m_model->setSetting("MY_GRIDSQUARE",    m_myGrid->text().trimmed());
    m_model->setSetting("MY_STATE",         m_myState->text().trimmed().toUpper());
    m_model->setSetting("DEFAULT_TX_PWR",   QString::number(m_defaultTxPwr->value(), 'f', 1));
    m_model->setSetting("MY_OPERATOR",      m_myOperator->text().trimmed().toUpper());

    const QString source = m_radioSource->currentData().toString();
    const bool rig = source == QLatin1String("rigctld");
    m_model->setSetting("RADIO_SOURCE", source);

    // Host and port are saved against the SELECTED source, so switching back
    // to TCI later finds its own endpoint rather than a CAT one.
    const QString host = m_tciHost->text().trimmed().isEmpty()
                             ? QStringLiteral("127.0.0.1")
                             : m_tciHost->text().trimmed();
    const QString port = QString::number(m_tciPort->value());
    m_model->setSetting(rig ? "RIGCTLD_HOST" : "TCI_HOST", host);
    m_model->setSetting(rig ? "RIGCTLD_PORT" : "TCI_PORT", port);
    m_model->setSetting("HAMLIB_RIGCTLD_PATH", m_rigctldPath->text().trimmed());
    m_model->setSetting("TCI_AUTOCONNECT",  m_tciAutoConnect->isChecked() ? "1" : "0");

    // Key the nickname off the host:port as SAVED, not as loaded — if the
    // user repointed this dialog at a different radio, the name they just
    // typed belongs to the new endpoint, not the old one.
    m_model->setSetting(rig ? rigctldNicknameKey(host, port) : tciNicknameKey(host, port),
                        m_tciNickname->text().trimmed());

    m_model->setSetting("DXC_ENABLE",       m_dxcEnable->isChecked() ? "1" : "0");
    m_model->setSetting("DXC_AUTODETECT",   m_dxcAutoDetect->isChecked() ? "1" : "0");
    m_model->setSetting("DXC_HOST",         m_dxcHost->text().trimmed());
    m_model->setSetting("DXC_PORT",         QString::number(m_dxcPort->value()));
    m_model->setSetting("DXC_CALLSIGN",     m_dxcCallsign->text().trimmed().toUpper());
    // Stored value is the canonical suffix string (the part actually appended
    // to the callsign).  If the user typed something custom we save the raw
    // text; otherwise we save the data() slot of the selected combo entry.
    {
        const int idx = m_dxcLoginSuffix->currentIndex();
        QString suffix;
        if (idx >= 0 && m_dxcLoginSuffix->currentText() == m_dxcLoginSuffix->itemText(idx)) {
            suffix = m_dxcLoginSuffix->itemData(idx).toString();
        } else {
            suffix = m_dxcLoginSuffix->currentText().trimmed();
        }
        m_model->setSetting("DXC_LOGIN_SUFFIX", suffix);
    }

    m_model->setSetting("POTA_ENABLE",   m_potaEnable->isChecked() ? "1" : "0");
    m_model->setSetting("POTA_POLL_SEC", QString::number(m_potaPollSec->value()));

    m_model->setSetting("LOOKUP_WORKEDBEFORE", m_lkWorkedBefore->isChecked() ? "1" : "0");
    m_model->setSetting("LOOKUP_CTY",          m_lkCty->isChecked() ? "1" : "0");
    m_model->setSetting("LOOKUP_PROVIDER",     m_lkProvider->currentData().toString());
    m_model->setSetting("LOOKUP_USERNAME",     m_lkUser->text().trimmed());
    m_model->setSetting("LOOKUP_PASSWORD",     m_lkPass->text());
    m_model->setSetting("LOOKUP_CALLOOK",      m_lkCallook->isChecked() ? "1" : "0");

    m_model->setSetting("CONTEST_MODE",     m_contestMode->isChecked() ? "1" : "0");
    m_model->setSetting("CONTEST_ID",       m_contestId->currentText().trimmed().toUpper());
    m_model->setSetting("CONTEST_STX_NEXT", QString::number(m_stxNext->value()));

    m_model->setSetting("CABRILLO_NAME",      m_cbName->text().trimmed());
    m_model->setSetting("CABRILLO_ADDRESS",   m_cbAddress->text().trimmed());
    m_model->setSetting("CABRILLO_EMAIL",     m_cbEmail->text().trimmed());
    m_model->setSetting("CABRILLO_CLUB",      m_cbClub->text().trimmed());
    m_model->setSetting("CABRILLO_LOCATION",  m_cbLocation->text().trimmed());
    m_model->setSetting("CABRILLO_CAT_OPERATOR",    m_cbCatOp->currentText());
    m_model->setSetting("CABRILLO_CAT_ASSISTED",    m_cbCatAssisted->currentText());
    m_model->setSetting("CABRILLO_CAT_BAND",        m_cbCatBand->currentText());
    m_model->setSetting("CABRILLO_CAT_MODE",        m_cbCatMode->currentText());
    m_model->setSetting("CABRILLO_CAT_POWER",       m_cbCatPower->currentText());
    m_model->setSetting("CABRILLO_CAT_STATION",     m_cbCatStation->currentText());
    m_model->setSetting("CABRILLO_CAT_TRANSMITTER", m_cbCatTransmitter->currentText());

    accept();
}

} // namespace ShackLog
