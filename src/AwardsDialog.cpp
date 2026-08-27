#include "AwardsDialog.h"

#include "LogbookModel.h"

#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace ShackBook {

namespace {

// Sorted, comma-joined chase list — the heart of the panel.
QString joined(const QSet<QString>& set)
{
    QStringList l(set.cbegin(), set.cend());
    std::sort(l.begin(), l.end());
    return l.join(QStringLiteral(", "));
}

QString joinedInts(const QSet<int>& set)
{
    QList<int> l(set.cbegin(), set.cend());
    std::sort(l.begin(), l.end());
    QStringList out;
    out.reserve(l.size());
    for (int v : l) out << QString::number(v);
    return out.join(QStringLiteral(", "));
}

template <typename T>
QSet<T> minus(const QSet<T>& all, const QSet<T>& have)
{
    QSet<T> m = all;
    m.subtract(have);
    return m;
}

} // namespace

AwardsDialog::AwardsDialog(LogbookModel* model, const QString& operatorCall,
                           QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Awards — %1").arg(operatorCall));
    resize(640, 520);

    auto* lay = new QVBoxLayout(this);

    auto* report = new QPlainTextEdit(this);
    report->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    report->setFont(mono);
    report->setPlainText(buildReport(model, operatorCall));
    lay->addWidget(report);

    auto* hint = new QLabel(
        QStringLiteral("Confirmed = LoTW or QSL card received (eQSL is not "
                       "accepted for ARRL awards). Text is selectable — "
                       "copy a chase list straight out."),
        this);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    lay->addWidget(closeBtn, 0, Qt::AlignRight);
}

QString AwardsDialog::buildReport(LogbookModel* model,
                                  const QString& operatorCall) const
{
    const auto a = model->awardsSummary();

    static const QSet<QString> kStates = {
        "AL","AK","AZ","AR","CA","CO","CT","DE","FL","GA",
        "HI","ID","IL","IN","IA","KS","KY","LA","ME","MD",
        "MA","MI","MN","MS","MO","MT","NE","NV","NH","NJ",
        "NM","NY","NC","ND","OH","OK","OR","PA","RI","SC",
        "SD","TN","TX","UT","VT","VA","WA","WV","WI","WY"};
    static const QSet<QString> kConts = {"NA","SA","EU","AF","AS","OC"};
    QSet<int> kZones;
    for (int z = 1; z <= 40; ++z) kZones.insert(z);

    QString r;
    r += QStringLiteral("Awards summary — %1  (%2 QSOs scanned)\n")
             .arg(operatorCall)
             .arg(a.qsoCount);
    r += QStringLiteral("─────────────────────────────────────────────\n\n");

    r += QStringLiteral("DXCC   entities      worked %1     confirmed %2\n")
             .arg(a.dxccWorked.size()).arg(a.dxccConfirmed.size());
    r += QStringLiteral("       (counts not yet validated against the "
                        "current/deleted entity list)\n\n");

    r += QStringLiteral("WAS    US states     worked %1/50  confirmed %2/50\n")
             .arg(a.wasWorked.size()).arg(a.wasConfirmed.size());
    const auto wasNeedW = minus(kStates, a.wasWorked);
    const auto wasNeedC = minus(kStates, a.wasConfirmed);
    if (wasNeedW.isEmpty())
        r += QStringLiteral("       WORKED ALL 50!\n");
    else
        r += QStringLiteral("       still to work:    %1\n").arg(joined(wasNeedW));
    if (!wasNeedC.isEmpty() && wasNeedC.size() < 50)
        r += QStringLiteral("       still to confirm: %1\n").arg(joined(wasNeedC));
    r += QLatin1Char('\n');

    r += QStringLiteral("WAC    continents    worked %1/6   confirmed %2/6\n")
             .arg(a.wacWorked.size()).arg(a.wacConfirmed.size());
    const auto wacNeedW = minus(kConts, a.wacWorked);
    if (!wacNeedW.isEmpty())
        r += QStringLiteral("       still to work:    %1\n").arg(joined(wacNeedW));
    r += QLatin1Char('\n');

    r += QStringLiteral("WAZ    CQ zones      worked %1/40  confirmed %2/40\n")
             .arg(a.wazWorked.size()).arg(a.wazConfirmed.size());
    const auto wazNeedW = minus(kZones, a.wazWorked);
    if (wazNeedW.isEmpty())
        r += QStringLiteral("       ALL 40 ZONES WORKED!\n");
    else
        r += QStringLiteral("       still to work:    zone %1\n").arg(joinedInts(wazNeedW));
    r += QLatin1Char('\n');

    r += QStringLiteral("Grids  4-char squares worked %1   (VUCC per-band — phase 2)\n")
             .arg(a.gridsWorked.size());

    if (!a.wasBogus.isEmpty()) {
        r += QStringLiteral("\nIgnored out-of-list STATE values (data hygiene): %1\n")
                 .arg(joined(a.wasBogus));
    }
    return r;
}

} // namespace ShackBook
