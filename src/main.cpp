#include "mainwindow.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

// OliVision colors: navy blue with the green of the Kreis 13 app
static void applyOliVisionTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    const QColor navy("#0d1b2a");
    const QColor panel("#16293d");
    const QColor raised("#1a3550");
    const QColor line("#2a4a6a");
    const QColor text("#e8eef4");
    const QColor muted("#a0b8cc");
    const QColor green("#39d353");

    QPalette palette;
    palette.setColor(QPalette::Window, navy);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, panel);
    palette.setColor(QPalette::AlternateBase, raised);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Button, raised);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, green);
    palette.setColor(QPalette::Highlight, green);
    palette.setColor(QPalette::HighlightedText, navy);
    palette.setColor(QPalette::Link, green);
    palette.setColor(QPalette::LinkVisited, green);
    palette.setColor(QPalette::ToolTipBase, raised);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Light, line);
    palette.setColor(QPalette::Midlight, line);
    palette.setColor(QPalette::Mid, line);
    palette.setColor(QPalette::Dark, navy);
    palette.setColor(QPalette::Shadow, QColor("#050b12"));
    palette.setColor(QPalette::Disabled, QPalette::Text, muted.darker(140));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted.darker(140));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, muted.darker(140));
    app.setPalette(palette);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("OliVision");
    applyOliVisionTheme(a);
    MainWindow w;
    w.show();
    return a.exec();
}
