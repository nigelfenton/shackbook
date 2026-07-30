#include "LotwDialog.h"
#include "AdifReader.h"
#include "LogbookModel.h"

#include <QDateEdit>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace ShackLog {

namespace {

// Settings keys (per-log settings table, alongside the lookup credentials).
const auto kKeyTqslPath = QStringLiteral("LOTW_TQSL_PATH");
const auto kKeyLocation = QStringLiteral("LOTW_STATION_LOCATION");
const auto kKeyUser     = QStringLiteral("LOTW_USERNAME");
const auto kKeyPass     = QStringLiteral("LOTW_PASSWORD");
const auto kKeySince    = QStringLiteral("LOTW_QSL_SINCE");

// tqsl's documented command-line exit statuses (Help → "Command line
// options"). The three that matter for marking QSOs sent: 0 = uploaded,
// 8 = every QSO was already uploaded (a dupe IS at LoTW — mark it), and
// 9 = mixed, new ones uploaded and dupes skipped.
QString tqslStatusText(int code)
{
    switch (code) {
    case 0:  return QStringLiteral("success — all QSOs signed and uploaded");
    case 1:  return QStringLiteral("cancelled by user");
    case 2:  return QStringLiteral("rejected by LoTW");
    case 3:  return QStringLiteral("unexpected LoTW response");
    case 4:  return QStringLiteral("TQSL error");
    case 5:  return QStringLiteral("TQSL library error");
    case 6:  return QStringLiteral("could not open input file");
    case 7:  return QStringLiteral("could not open output file");
    case 8:  return QStringLiteral("all QSOs were already uploaded (duplicates)");
    case 9:  return QStringLiteral("some QSOs uploaded, the rest were already at LoTW");
    case 10: return QStringLiteral("command syntax error");
    case 11: return QStringLiteral("LoTW connection failed");
    default: return QStringLiteral("unknown status %1").arg(code);
    }
}

} // namespace

LotwDialog::LotwDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent), m_model(model), m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle(tr("LoTW — Sign, Upload && Confirmations"));
    resize(560, 620);

    auto* top = new QVBoxLayout(this);

    m_guidance = new QLabel(this);
    m_guidance->setWordWrap(true);
    m_guidance->setTextFormat(Qt::RichText);
    m_guidance->setOpenExternalLinks(true);
    top->addWidget(m_guidance);

    // ── Sign & upload ────────────────────────────────────────────────
    auto* upBox = new QGroupBox(tr("Sign && upload with TQSL"), this);
    auto* upForm = new QFormLayout(upBox);

    auto* pathRow = new QHBoxLayout;
    m_tqslPath = new QLineEdit(m_model->settingValue(kKeyTqslPath), upBox);
    m_tqslPath->setPlaceholderText(tr("auto-detect"));
    auto* browse = new QPushButton(tr("Browse…"), upBox);
    connect(browse, &QPushButton::clicked, this, &LotwDialog::onBrowseTqsl);
    pathRow->addWidget(m_tqslPath, 1);
    pathRow->addWidget(browse);
    upForm->addRow(tr("tqsl program"), pathRow);

    m_location = new QLineEdit(m_model->settingValue(kKeyLocation), upBox);
    m_location->setPlaceholderText(tr("as created in TQSL: Station → Add Location"));
    upForm->addRow(tr("Station location"), m_location);

    m_rangeUnsent = new QRadioButton(upBox);   // text set by refreshUnsentCount()
    m_rangeUnsent->setChecked(true);
    upForm->addRow(m_rangeUnsent);

    auto* dr = new QHBoxLayout;
    m_rangeDates = new QRadioButton(tr("Date range"), upBox);
    m_dateFrom = new QDateEdit(QDate::currentDate().addMonths(-1), upBox);
    m_dateTo   = new QDateEdit(QDate::currentDate(), upBox);
    for (QDateEdit* d : {m_dateFrom, m_dateTo}) {
        d->setCalendarPopup(true);
        d->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    }
    dr->addWidget(m_rangeDates);
    dr->addWidget(m_dateFrom);
    dr->addWidget(new QLabel(QStringLiteral("→"), upBox));
    dr->addWidget(m_dateTo);
    dr->addStretch(1);
    upForm->addRow(dr);

    m_btnUpload = new QPushButton(tr("Sign && Upload"), upBox);
    connect(m_btnUpload, &QPushButton::clicked, this, &LotwDialog::onSignUpload);
    upForm->addRow(m_btnUpload);
    top->addWidget(upBox);

    // ── Confirmations ────────────────────────────────────────────────
    auto* dnBox = new QGroupBox(tr("Fetch confirmations"), this);
    auto* dnForm = new QFormLayout(dnBox);

    m_user = new QLineEdit(m_model->settingValue(kKeyUser), dnBox);
    dnForm->addRow(tr("LoTW username"), m_user);
    m_pass = new QLineEdit(m_model->settingValue(kKeyPass), dnBox);
    m_pass->setEchoMode(QLineEdit::Password);
    dnForm->addRow(tr("LoTW password"), m_pass);
    auto* credNote = new QLabel(
        tr("Your LoTW <i>website</i> login — not the certificate passphrase."), dnBox);
    credNote->setTextFormat(Qt::RichText);
    dnForm->addRow(QString(), credNote);

    m_since = new QDateEdit(dnBox);
    m_since->setCalendarPopup(true);
    m_since->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    const QString since = m_model->settingValue(kKeySince);
    m_since->setDate(since.isEmpty()
                         ? QDate::currentDate().addYears(-1)
                         : QDate::fromString(since, QStringLiteral("yyyy-MM-dd")));
    dnForm->addRow(tr("QSLs since"), m_since);

    m_btnFetch = new QPushButton(tr("Fetch && Apply"), dnBox);
    connect(m_btnFetch, &QPushButton::clicked, this, &LotwDialog::onFetchConfirmations);
    dnForm->addRow(m_btnFetch);
    top->addWidget(dnBox);

    // ── Activity log ─────────────────────────────────────────────────
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(tr("tqsl output and fetch results appear here"));
    top->addWidget(m_log, 1);

    refreshUnsentCount();
    refreshGuidance();
}

QString LotwDialog::tqslPath() const
{
    const QString configured = m_tqslPath->text().trimmed();
    if (!configured.isEmpty()) return configured;
    return autoDetectTqsl();
}

QString LotwDialog::autoDetectTqsl()
{
    const QStringList candidates = {
#ifdef Q_OS_WIN
        QStringLiteral("C:/Program Files (x86)/TrustedQSL/tqsl.exe"),
        QStringLiteral("C:/Program Files/TrustedQSL/tqsl.exe"),
#elif defined(Q_OS_MACOS)
        QStringLiteral("/Applications/TrustedQSL/tqsl.app/Contents/MacOS/tqsl"),
        QStringLiteral("/Applications/tqsl.app/Contents/MacOS/tqsl"),
#endif
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return c;
    // PATH lookup covers Linux packages and custom installs everywhere.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("tqsl"));
    return onPath;   // empty = not found
}

void LotwDialog::refreshGuidance()
{
    const QString path = tqslPath();
    if (path.isEmpty()) {
        m_guidance->setText(tr(
            "<b>One-time setup:</b> uploading needs ARRL's free TrustedQSL "
            "(tqsl) with your callsign certificate. "
            "<a href=\"https://www.arrl.org/tqsl-download\">Download TQSL</a>, "
            "then request your certificate inside it (Callsign Certificates → "
            "Request New…) and add a Station Location. When tqsl is installed "
            "it will be picked up here automatically."));
        m_btnUpload->setEnabled(false);
    } else {
        m_guidance->setText(tr("Using tqsl: %1").arg(QDir::toNativeSeparators(path)));
        m_btnUpload->setEnabled(true);
    }
}

void LotwDialog::refreshUnsentCount()
{
    LogbookFilter f;
    f.lotwUnsentOnly = true;
    const int n = m_model->queryQsos(f).size();
    m_rangeUnsent->setText(tr("All not yet uploaded (%1 QSOs)").arg(n));
}

void LotwDialog::appendLog(const QString& line)
{
    m_log->appendPlainText(line);
}

void LotwDialog::persistSettings()
{
    m_model->setSetting(kKeyTqslPath, m_tqslPath->text().trimmed());
    m_model->setSetting(kKeyLocation, m_location->text().trimmed());
    m_model->setSetting(kKeyUser, m_user->text().trimmed());
    m_model->setSetting(kKeyPass, m_pass->text());
}

void LotwDialog::onBrowseTqsl()
{
#ifdef Q_OS_WIN
    const QString filter = tr("tqsl (tqsl.exe)");
#else
    const QString filter = tr("tqsl (tqsl)");
#endif
    const QString path = QFileDialog::getOpenFileName(this, tr("Locate tqsl"),
                                                      QString(), filter);
    if (!path.isEmpty()) {
        m_tqslPath->setText(path);
        refreshGuidance();
    }
}

void LotwDialog::onSignUpload()
{
    persistSettings();

    const QString loc = m_location->text().trimmed();
    if (loc.isEmpty()) {
        appendLog(tr("✗ Station location is required — create one in TQSL "
                     "(Station → Add Location), then enter its name here."));
        return;
    }

    LogbookFilter f;
    if (m_rangeDates->isChecked()) {
        f.dateFrom = m_dateFrom->date().toString(QStringLiteral("yyyyMMdd"));
        f.dateTo   = m_dateTo->date().toString(QStringLiteral("yyyyMMdd"));
    } else {
        f.lotwUnsentOnly = true;
    }

    const QVector<Qso> rows = m_model->queryQsos(f);
    if (rows.isEmpty()) {
        appendLog(tr("Nothing to upload — no QSOs match."));
        return;
    }

    // The temp ADIF must outlive tqsl, which runs asynchronously.
    auto* adi = new QTemporaryFile(QDir::temp().filePath(
                                       QStringLiteral("shacklog-lotw-XXXXXX.adi")),
                                   this);
    if (!adi->open()) {
        appendLog(tr("✗ could not create a temporary ADIF: %1").arg(adi->errorString()));
        return;
    }
    const QString adiPath = adi->fileName();
    adi->close();
    if (m_model->exportAdif(adiPath, f) < 0) {
        appendLog(tr("✗ ADIF export failed: %1").arg(m_model->errorString()));
        return;
    }

    QVector<qint64> ids;
    ids.reserve(rows.size());
    for (const Qso& q : rows) ids.append(q.id);

    appendLog(tr("Signing and uploading %1 QSOs via tqsl…").arg(rows.size()));
    m_btnUpload->setEnabled(false);

    // -x batch (exit when done), -q no GUI progress, -u upload after signing,
    // -a compliant: sign what's new, silently skip QSOs LoTW already has.
    // If the certificate has a passphrase, tqsl prompts in its own window.
    auto* proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this,
            [this, proc, ids](int code, QProcess::ExitStatus st) {
        const QString output = QString::fromLocal8Bit(proc->readAll()).trimmed();
        if (!output.isEmpty()) appendLog(output);
        proc->deleteLater();
        m_btnUpload->setEnabled(true);
        if (st != QProcess::NormalExit) {
            appendLog(tr("✗ tqsl crashed"));
            return;
        }
        appendLog(tr("tqsl: %1").arg(tqslStatusText(code)));
        if (code == 0 || code == 8 || code == 9) {
            const QString today =
                QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
            const int n = m_model->markLotwSent(ids, today);
            appendLog(tr("✓ marked %1 QSOs as uploaded (LOTW_QSLSDATE %2)")
                          .arg(n).arg(today));
            refreshUnsentCount();
        } else {
            appendLog(tr("✗ QSOs NOT marked as uploaded"));
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        appendLog(tr("✗ could not run tqsl: %1").arg(proc->errorString()));
        proc->deleteLater();
        m_btnUpload->setEnabled(true);
    });
    proc->start(tqslPath(),
                {QStringLiteral("-x"), QStringLiteral("-q"),
                 QStringLiteral("-a"), QStringLiteral("compliant"),
                 QStringLiteral("-u"),
                 QStringLiteral("-l"), loc,
                 QDir::toNativeSeparators(adiPath)});
}

void LotwDialog::onFetchConfirmations()
{
    persistSettings();

    const QString user = m_user->text().trimmed();
    const QString pass = m_pass->text();
    if (user.isEmpty() || pass.isEmpty()) {
        appendLog(tr("✗ LoTW username and password are required for the "
                     "confirmation fetch."));
        return;
    }

    QUrl url(QStringLiteral("https://lotw.arrl.org/lotwuser/lotwreport.adi"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("login"), user);
    q.addQueryItem(QStringLiteral("password"), pass);
    q.addQueryItem(QStringLiteral("qso_query"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("qso_qsl"), QStringLiteral("yes"));
    q.addQueryItem(QStringLiteral("qso_qslsince"),
                   m_since->date().toString(QStringLiteral("yyyy-MM-dd")));
    url.setQuery(q);

    appendLog(tr("Fetching confirmations since %1…")
                  .arg(m_since->date().toString(QStringLiteral("yyyy-MM-dd"))));
    m_btnFetch->setEnabled(false);

    QNetworkReply* reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_btnFetch->setEnabled(true);
        if (reply->error() != QNetworkReply::NoError) {
            appendLog(tr("✗ fetch failed: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray body = reply->readAll();
        // A bad login comes back as an HTML page, not ADIF.
        if (body.left(512).toLower().contains("<html")) {
            appendLog(tr("✗ LoTW did not return a report — check username/"
                         "password (this is the lotw.arrl.org website login)."));
            return;
        }

        int records = 0, matched = 0;
        qsizetype pos = 0;
        QHash<QString, QString> fields;
        while (Adif::nextRecord(body, pos, &fields)) {
            ++records;
            const QString call = fields.value(QStringLiteral("call"));
            const QString band = fields.value(QStringLiteral("band"));
            const QString date = fields.value(QStringLiteral("qso_date"));
            const QString time = fields.value(QStringLiteral("time_on"));
            QString rdate      = fields.value(QStringLiteral("qslrdate"));
            if (rdate.isEmpty())
                rdate = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
            if (call.isEmpty() || band.isEmpty() || date.isEmpty()) continue;
            const int n = m_model->applyLotwConfirmation(call, band, date, time, rdate);
            if (n > 0) matched += n;
            else appendLog(tr("  no match in this log: %1 %2 %3").arg(call, band, date));
        }
        appendLog(tr("✓ %1 confirmations in report, %2 applied to this log")
                      .arg(records).arg(matched));
        if (records > 0) {
            m_model->setSetting(kKeySince,
                                QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
            m_since->setDate(QDate::currentDate());
        }
    });
}

} // namespace ShackLog
