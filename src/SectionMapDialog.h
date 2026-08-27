#pragma once

// SectionMapDialog — ARRL/RAC section map, N3FJP-style.
//
// An 86-tile cartogram (71 US + 14 RAC sections + DX, the official ARRL
// multiplier list) laid out roughly geographically.  Sections light up
// green as they're worked; tiles show the QSO count and the tooltip names
// the first station worked there.  The section is parsed from each QSO's
// received exchange (SRX_STRING, e.g. "3A MDC" → MDC) — the Field Day /
// Sweepstakes convention.
//
// Non-modal and live: refreshes whenever the logbook changes, so it can
// sit on a second monitor during the contest.  The same tile layout drives
// the web map on shack-hub (/notes/fdmap.html) — keep them in step.

#include <QDialog>
#include <QHash>

class QLabel;

namespace ShackBook {

class LogbookModel;

class SectionMapDialog : public QDialog {
    Q_OBJECT
public:
    explicit SectionMapDialog(LogbookModel* model, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    LogbookModel* m_model;
    QLabel* m_countLabel{};
    QLabel* m_statsLabel{};
    QHash<QString, QLabel*> m_tiles;     // section abbrev → tile
};

} // namespace ShackBook
