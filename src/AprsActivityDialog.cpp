#include "AprsActivityDialog.h"

#include "AprsKissClient.h"
#include "AprsStationModel.h"
#include "LogbookModel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace ShackBook {

namespace {
constexpr const char* kDefaultHost = "127.0.0.1:8001";
constexpr int kPruneIntervalMs = 10 * 1000; // age out stale rows every 10 s

// Split "host:port" -> (host, port). Defaults to 8001 if no port given.
void splitHostPort(const QString& in, QString* host, quint16* port)
{
    const QString s = in.trimmed();
    const int colon = s.lastIndexOf(':');
    if (colon > 0) {
        *host = s.left(colon);
        const quint16 p = s.mid(colon + 1).toUShort();
        *port = p ? p : 8001;
    } else {
        *host = s;
        *port = 8001;
    }
}
} // namespace

AprsActivityDialog::AprsActivityDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(QStringLiteral("APRS Activity — AetherSDR TNC"));
    resize(720, 460);

    m_client   = new AprsKissClient(this);
    m_stations = new AprsStationModel(this);
    m_stations->setMyGrid(m_model ? m_model->myGridsquare() : QString());

    // Sort the roster (default: most-recently-heard first via the Heard column
    // isn't numeric, so sort on Distance ascending when a grid is set; the user
    // can click any header).
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_stations);
    m_proxy->setSortRole(Qt::DisplayRole);

    // ── Connection row ──────────────────────────────────────────────
    m_hostEdit = new QLineEdit;
    {
        QSettings s;
        m_hostEdit->setText(s.value(QStringLiteral("aprs/host"),
                                    QString::fromLatin1(kDefaultHost)).toString());
    }
    m_hostEdit->setMaximumWidth(180);
    m_connectBtn = new QPushButton(QStringLiteral("Connect"));
    m_statusLabel = new QLabel(QStringLiteral("Disconnected"));
    m_countLabel  = new QLabel(QStringLiteral("0 stations"));

    auto* connRow = new QHBoxLayout;
    connRow->addWidget(new QLabel(QStringLiteral("TNC:")));
    connRow->addWidget(m_hostEdit);
    connRow->addWidget(m_connectBtn);
    connRow->addSpacing(12);
    connRow->addWidget(m_statusLabel);
    connRow->addStretch(1);
    connRow->addWidget(m_countLabel);

    // ── Roster table ────────────────────────────────────────────────
    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setSortingEnabled(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);

    // ── Message send row (Stage-3 seed) ─────────────────────────────
    m_toEdit  = new QLineEdit;
    m_toEdit->setPlaceholderText(QStringLiteral("To (call)"));
    m_toEdit->setMaximumWidth(120);
    m_msgEdit = new QLineEdit;
    m_msgEdit->setPlaceholderText(QStringLiteral("Message…"));
    m_msgEdit->setMaxLength(67); // APRS message text limit
    m_sendBtn = new QPushButton(QStringLiteral("Send"));
    m_sendBtn->setEnabled(false);

    auto* msgRow = new QHBoxLayout;
    msgRow->addWidget(new QLabel(QStringLiteral("Msg:")));
    msgRow->addWidget(m_toEdit);
    msgRow->addWidget(m_msgEdit, 1);
    msgRow->addWidget(m_sendBtn);

    auto* root = new QVBoxLayout(this);
    root->addLayout(connRow);
    root->addWidget(m_table, 1);
    root->addLayout(msgRow);

    // ── Wiring ──────────────────────────────────────────────────────
    connect(m_connectBtn, &QPushButton::clicked, this, &AprsActivityDialog::onConnectClicked);
    connect(m_sendBtn,    &QPushButton::clicked, this, &AprsActivityDialog::onSendClicked);
    connect(m_msgEdit,    &QLineEdit::returnPressed, this, &AprsActivityDialog::onSendClicked);

    connect(m_client, &AprsKissClient::connectionChanged,
            this, &AprsActivityDialog::onConnectionChanged);
    connect(m_client, &AprsKissClient::aprsReport,
            m_stations, [this](const Aprs::Report& r) { m_stations->addReport(r); });
    connect(m_stations, &AprsStationModel::rosterChanged,
            this, &AprsActivityDialog::onRosterChanged);

    m_pruneTimer = new QTimer(this);
    m_pruneTimer->setInterval(kPruneIntervalMs);
    connect(m_pruneTimer, &QTimer::timeout, this, &AprsActivityDialog::onPruneTick);
    m_pruneTimer->start();

    refreshWorkedCalls();
    setStatus(QStringLiteral("Disconnected"), false);
}

AprsActivityDialog::~AprsActivityDialog() = default;

void AprsActivityDialog::onConnectClicked()
{
    if (m_client->connected()) {
        m_client->disconnectFromServer();
        return;
    }
    QString host;
    quint16 port = 8001;
    splitHostPort(m_hostEdit->text(), &host, &port);
    if (host.isEmpty()) {
        setStatus(QStringLiteral("Enter a host, e.g. 127.0.0.1:8001"), false);
        return;
    }
    QSettings().setValue(QStringLiteral("aprs/host"), m_hostEdit->text().trimmed());
    setStatus(QStringLiteral("Connecting to %1:%2…").arg(host).arg(port), false);
    m_client->connectToServer(host, port);
}

void AprsActivityDialog::onConnectionChanged(bool connected)
{
    m_connectBtn->setText(connected ? QStringLiteral("Disconnect")
                                    : QStringLiteral("Connect"));
    m_sendBtn->setEnabled(connected);
    m_hostEdit->setEnabled(!connected);
    if (connected) {
        setStatus(QStringLiteral("Connected to %1:%2")
                      .arg(m_client->host()).arg(m_client->port()),
                  true);
        refreshWorkedCalls(); // in case the log changed since the dialog opened
    } else {
        const QString err = m_client->lastError();
        setStatus(err.isEmpty() ? QStringLiteral("Disconnected")
                                : QStringLiteral("Disconnected — %1").arg(err),
                  false);
    }
}

void AprsActivityDialog::onSendClicked()
{
    if (!m_client->connected())
        return;
    const QString to  = m_toEdit->text().trimmed().toUpper();
    const QString txt = m_msgEdit->text();
    const QString me  = m_model ? m_model->myCall().trimmed().toUpper() : QString();
    if (to.isEmpty() || txt.isEmpty()) {
        setStatus(QStringLiteral("Enter a recipient call and message text"), false);
        return;
    }
    if (me.isEmpty()) {
        setStatus(QStringLiteral("Set MY_CALL in Settings before sending"), false);
        return;
    }
    // Path WIDE1-1 is a sane default for a single local digipeat hop.
    const bool ok = m_client->sendMessage(me, to, txt, {QStringLiteral("WIDE1-1")});
    if (ok) {
        setStatus(QStringLiteral("Sent to %1").arg(to), true);
        m_msgEdit->clear();
    } else {
        setStatus(QStringLiteral("Send failed (not connected?)"), false);
    }
}

void AprsActivityDialog::onRosterChanged(int count)
{
    m_countLabel->setText(count == 1 ? QStringLiteral("1 station")
                                     : QStringLiteral("%1 stations").arg(count));
}

void AprsActivityDialog::onPruneTick()
{
    m_stations->pruneStale();
    // Keep the "Heard" column's relative times current for idle rows.
    m_stations->touchHeard();
}

void AprsActivityDialog::refreshWorkedCalls()
{
    if (!m_model)
        return;
    QSet<QString> calls;
    const auto qsos = m_model->queryQsos({});
    for (const Qso& q : qsos)
        if (!q.call.isEmpty())
            calls.insert(q.call);
    m_stations->setWorkedCalls(calls);
    m_stations->setMyGrid(m_model->myGridsquare());
}

void AprsActivityDialog::setStatus(const QString& text, bool ok)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(ok ? QStringLiteral("color: #1db954;")
                                    : QStringLiteral("color: #6b8099;"));
}

} // namespace ShackBook
