#include "MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QPalette>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TermDock"));
    QApplication::setApplicationDisplayName(QStringLiteral("终端舱"));
    QApplication::setQuitOnLastWindowClosed(false);
    app.setCursorFlashTime(530);

    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, QColor(17, 23, 29));
    palette.setColor(QPalette::WindowText, QColor(235, 242, 246));
    palette.setColor(QPalette::Base, QColor(10, 14, 18));
    palette.setColor(QPalette::AlternateBase, QColor(23, 32, 41));
    palette.setColor(QPalette::Button, QColor(28, 38, 48));
    palette.setColor(QPalette::ButtonText, QColor(235, 242, 246));
    palette.setColor(QPalette::Text, QColor(235, 242, 246));
    palette.setColor(QPalette::Highlight, QColor(53, 114, 153));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    app.setPalette(palette);
    app.setStyleSheet(QStringLiteral(
        "QMainWindow { background: #111a22; }"
        "QStatusBar { background: #0e151b; color: #90a4b3; border-top: 1px solid #263543; }"
        "QToolTip { background: #162029; color: #e8f1f6; border: 1px solid #394c5b; padding: 4px 7px; }"
    ));

    MainWindow window;
    window.resize(1100, 720);
    window.show();

    return app.exec();
}
