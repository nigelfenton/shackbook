#pragma once

// AwardsDialog — log-scan award progress: DXCC, WAS, WAC, WAZ, grids.
//
// Phase 1 (see roadmap): whole-log Mixed standings with worked/confirmed
// counts and "still needed" chase lists, rendered as a selectable text
// report (copy-paste straight into club Discord).  Phase 2 adds per-band/
// per-mode variants, VUCC and DXCC Challenge; DXCC validation against a
// current-vs-deleted entity table arrives with the built-in DXCC table.

#include <QDialog>

namespace ShackBook {

class LogbookModel;

class AwardsDialog : public QDialog {
    Q_OBJECT

public:
    AwardsDialog(LogbookModel* model, const QString& operatorCall,
                 QWidget* parent = nullptr);

private:
    QString buildReport(LogbookModel* model, const QString& operatorCall) const;
};

} // namespace ShackBook
