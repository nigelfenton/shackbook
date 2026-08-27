#include "SectionMapDialog.h"

#include "LogbookModel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace ShackBook {

namespace {

struct SectionDef {
    const char* abbr;
    const char* name;
    int col;
    int row;
};

// Official ARRL/RAC section list (contests.arrl.org) + DX = 86 tiles.
// Hand-laid cartogram, roughly geographic — mirrors /notes/fdmap.html on
// shack-hub; change both together.
const SectionDef kSections[] = {
    {"AK","Alaska",0,0},{"TER","NT/YT/NU Territories",2,0},{"NL","Newfoundland/Labrador",15,0},
    {"BC","British Columbia",0,1},{"AB","Alberta",1,1},{"SK","Saskatchewan",2,1},
    {"MB","Manitoba",3,1},{"ONN","Ontario North",5,1},{"QC","Quebec",7,1},
    {"NB","New Brunswick",13,1},{"PE","Prince Edward Is.",14,1},{"NS","Nova Scotia",15,1},
    {"WWA","Western Washington",0,2},{"EWA","Eastern Washington",1,2},{"MT","Montana",2,2},
    {"ND","North Dakota",3,2},{"ONS","Ontario South",5,2},{"GH","Golden Horseshoe",6,2},
    {"ONE","Ontario East",7,2},{"NNY","Northern New York",9,2},{"VT","Vermont",10,2},
    {"NH","New Hampshire",11,2},{"ME","Maine",12,2},
    {"OR","Oregon",0,3},{"ID","Idaho",1,3},{"WY","Wyoming",2,3},{"SD","South Dakota",3,3},
    {"MN","Minnesota",4,3},{"WI","Wisconsin",5,3},{"MI","Michigan",6,3},
    {"WNY","Western New York",8,3},{"ENY","Eastern New York",9,3},
    {"WMA","Western Massachusetts",10,3},{"EMA","Eastern Massachusetts",11,3},
    {"RI","Rhode Island",12,3},
    {"SV","Sacramento Valley",0,4},{"NV","Nevada",1,4},{"UT","Utah",2,4},{"NE","Nebraska",3,4},
    {"IA","Iowa",4,4},{"IL","Illinois",5,4},{"IN","Indiana",6,4},{"OH","Ohio",7,4},
    {"WPA","Western Pennsylvania",8,4},{"EPA","Eastern Pennsylvania",9,4},
    {"NNJ","Northern New Jersey",10,4},{"CT","Connecticut",11,4},{"NLI","NYC-Long Island",12,4},
    {"SF","San Francisco",0,5},{"EB","East Bay",1,5},{"CO","Colorado",2,5},{"KS","Kansas",3,5},
    {"MO","Missouri",4,5},{"KY","Kentucky",5,5},{"WV","West Virginia",6,5},{"VA","Virginia",7,5},
    {"MDC","Maryland-DC",8,5},{"DE","Delaware",9,5},{"SNJ","Southern New Jersey",10,5},
    {"SCV","Santa Clara Valley",0,6},{"SJV","San Joaquin Valley",1,6},{"AZ","Arizona",2,6},
    {"NM","New Mexico",3,6},{"OK","Oklahoma",4,6},{"AR","Arkansas",5,6},
    {"TN","Tennessee",6,6},{"NC","North Carolina",7,6},
    {"SB","Santa Barbara",0,7},{"LAX","Los Angeles",1,7},{"ORG","Orange",2,7},
    {"WTX","West Texas",3,7},{"NTX","North Texas",4,7},{"LA","Louisiana",5,7},
    {"MS","Mississippi",6,7},{"AL","Alabama",7,7},{"GA","Georgia",8,7},{"SC","South Carolina",9,7},
    {"SDG","San Diego",0,8},{"PAC","Pacific (HI etc.)",1,8},{"STX","South Texas",4,8},
    {"NFL","Northern Florida",8,8},{"PR","Puerto Rico",11,8},{"VI","US Virgin Is.",12,8},
    {"WCF","West Central Florida",7,9},{"SFL","Southern Florida",8,9},{"DX","DX",15,9},
};
constexpr int kSectionCount = static_cast<int>(sizeof(kSections) / sizeof(kSections[0]));

constexpr const char* kTileIdle =
    "QLabel { background-color: #0f1626; color: #6b8099; "
    "border: 1px solid #1c2a40; border-radius: 4px; "
    "font-family: Consolas, 'Cascadia Mono', monospace; font-size: 11px; }";
constexpr const char* kTileWorked =
    "QLabel { background-color: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
    "stop:0 #1db954, stop:1 #0e7a33); color: #04210e; "
    "border: 1px solid #1db954; border-radius: 4px; font-weight: bold; "
    "font-family: Consolas, 'Cascadia Mono', monospace; font-size: 11px; }";
constexpr const char* kCountStyle =
    "QLabel { color: #1db954; font-size: 22px; font-weight: bold; "
    "font-family: Consolas, 'Cascadia Mono', monospace; }";
constexpr const char* kStatsStyle =
    "QLabel { color: #6b8099; font-size: 11px; }";

} // namespace

SectionMapDialog::SectionMapDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle("Section Map — ARRL / RAC");
    setMinimumSize(900, 560);

    auto* main = new QVBoxLayout(this);
    main->setSpacing(8);

    auto* header = new QHBoxLayout;
    m_countLabel = new QLabel("0 / " + QString::number(kSectionCount));
    m_countLabel->setStyleSheet(kCountStyle);
    m_statsLabel = new QLabel;
    m_statsLabel->setStyleSheet(kStatsStyle);
    header->addWidget(m_countLabel);
    header->addSpacing(16);
    header->addWidget(m_statsLabel);
    header->addStretch();
    main->addLayout(header);

    auto* grid = new QGridLayout;
    grid->setSpacing(3);
    for (const auto& s : kSections) {
        auto* tile = new QLabel(QString::fromLatin1(s.abbr));
        tile->setAlignment(Qt::AlignCenter);
        tile->setMinimumSize(48, 38);
        tile->setStyleSheet(kTileIdle);
        tile->setToolTip(QString::fromLatin1(s.name));
        grid->addWidget(tile, s.row, s.col);
        m_tiles.insert(QString::fromLatin1(s.abbr), tile);
    }
    // Equal stretch so the map scales with the window.
    for (int c = 0; c < 16; ++c) grid->setColumnStretch(c, 1);
    for (int r = 0; r < 10; ++r) grid->setRowStretch(r, 1);
    main->addLayout(grid, /*stretch*/ 1);

    auto* foot = new QLabel(
        "Section = last token of the received exchange (SRX, e.g. “3A MDC” "
        "→ MDC). Live: updates as QSOs are logged.");
    foot->setStyleSheet(kStatsStyle);
    foot->setWordWrap(true);
    main->addWidget(foot);

    // Live refresh while the dialog is open.
    connect(m_model, &LogbookModel::qsoAdded,   this, &SectionMapDialog::refresh);
    connect(m_model, &LogbookModel::qsoUpdated, this, &SectionMapDialog::refresh);
    connect(m_model, &LogbookModel::qsoDeleted, this, &SectionMapDialog::refresh);

    refresh();
}

void SectionMapDialog::refresh()
{
    if (!m_model || !m_model->isOpen()) return;

    struct Hit { int count{0}; QString first; };
    QHash<QString, Hit> worked;
    int scanned = 0, noSection = 0;

    const auto qsos = m_model->queryQsos();      // whole log, newest first
    scanned = qsos.size();
    for (const auto& q : qsos) {
        // Last token of the exchange that names a real section wins.
        QString sect;
        const QStringList toks = q.srxString.trimmed().toUpper().split(
            QRegularExpression("[\\s,/]+"), Qt::SkipEmptyParts);
        for (int i = toks.size() - 1; i >= 0 && sect.isEmpty(); --i)
            if (m_tiles.contains(toks[i])) sect = toks[i];
        if (sect.isEmpty()) { ++noSection; continue; }
        auto& hit = worked[sect];
        ++hit.count;
        hit.first = q.call;        // list is newest-first → ends on the oldest
    }

    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        QLabel* tile = it.value();
        const auto hit = worked.constFind(it.key());
        if (hit != worked.constEnd()) {
            tile->setStyleSheet(kTileWorked);
            tile->setText(QString("%1\n%2").arg(it.key()).arg(hit->count));
            tile->setToolTip(QString("%1 — %2 QSO(s), first worked: %3")
                                 .arg(tile->toolTip().section(" —", 0, 0),
                                      QString::number(hit->count), hit->first));
        } else {
            tile->setStyleSheet(kTileIdle);
            tile->setText(it.key());
            tile->setToolTip(tile->toolTip().section(" —", 0, 0));
        }
    }

    m_countLabel->setText(QString("%1 / %2").arg(worked.size()).arg(kSectionCount));
    m_statsLabel->setText(QString("sections worked · %1 QSOs scanned · %2 with no section")
                              .arg(scanned).arg(noSection));
}

} // namespace ShackBook
