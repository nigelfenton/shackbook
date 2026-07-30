// ShackLog — standalone ham radio logbook with TCI integration.
// Entry point: build a QApplication, install a dark stylesheet to match
// the rest of the user's shack stack, and show the main window.

#include "MainWindow.h"

#include <QApplication>
#include <QFile>

namespace {

// Compact dark stylesheet — matches the AetherSDR / ShackController look.
// Only the basics; per-widget styling is applied inline by MainWindow.
constexpr const char* kAppStylesheet = R"(
QMainWindow, QDialog, QWidget {
    background-color: #0a1320;
    color: #dde6f0;
    font-family: "Segoe UI", "Helvetica Neue", sans-serif;
    font-size: 11px;
}
QMenuBar { background-color: #0d1e30; color: #dde6f0; }
QMenuBar::item:selected { background-color: #00475e; color: #00d8ef; }
QMenu { background-color: #0d1e30; border: 1px solid #1c2a40; }
QMenu::item:selected { background-color: #00475e; color: #00d8ef; }
QStatusBar { background-color: #0d1e30; color: #6b8099; }
QStatusBar::item { border: none; }
QPushButton {
    background-color: #0f1e30;
    border: 1px solid #1c2a40;
    border-radius: 3px;
    padding: 4px 12px;
    color: #dde6f0;
}
QPushButton:hover { background-color: #0e1a2e; border: 1px solid #00d8ef; }
QPushButton:disabled { color: #3a4a60; border: 1px solid #1c2a40; }
QComboBox {
    background-color: #0b1220;
    border: 1px solid #1c2a40;
    border-radius: 3px;
    padding: 3px 8px;
    color: #dde6f0;
}
QComboBox QAbstractItemView {
    background-color: #0d1e30;
    color: #dde6f0;
    selection-background-color: #00475e;
}
QSpinBox, QDoubleSpinBox {
    background-color: #0b1220;
    border: 1px solid #1c2a40;
    border-radius: 3px;
    padding: 2px 4px;
    color: #dde6f0;
}
QLineEdit {
    background-color: #0b1220;
    border: 1px solid #1c2a40;
    border-radius: 3px;
    padding: 4px 6px;
    color: #dde6f0;
}
QLineEdit:focus { border: 1px solid #00d8ef; }
QPlainTextEdit {
    background-color: #0b1220;
    border: 1px solid #1c2a40;
    color: #dde6f0;
}
QTableWidget {
    background-color: #0a1320;
    alternate-background-color: #0d1e30;
    gridline-color: #1c2a40;
    color: #dde6f0;
    selection-background-color: #00475e;
    selection-color: #00d8ef;
}
QHeaderView::section {
    background-color: #0d1e30;
    color: #6b8099;
    border: 1px solid #1c2a40;
    padding: 4px 6px;
    font-weight: bold;
}
QTabWidget::pane { border: 1px solid #1c2a40; background: #0a1320; }
QTabBar::tab {
    background: #0d1e30; color: #6b8099; padding: 5px 10px;
    border: 1px solid #1c2a40; border-bottom: none;
}
QTabBar::tab:selected { background: #00475e; color: #00d8ef; }
QCheckBox { color: #dde6f0; }
QGroupBox {
    border: 1px solid #1c2a40; border-radius: 3px; margin-top: 6px;
    padding: 6px;
}
QGroupBox::title { color: #6b8099; subcontrol-origin: margin; left: 8px; }
)";

} // namespace

int main(int argc, char* argv[])
{
    QApplication::setOrganizationName("G0JKN");
    QApplication::setOrganizationDomain("g0jkn.uk");
    QApplication::setApplicationName("ShackLog");
    QApplication::setApplicationVersion("0.4.0");

    QApplication app(argc, argv);
    app.setStyleSheet(kAppStylesheet);

    ShackLog::MainWindow w;
    w.show();
    return app.exec();
}
