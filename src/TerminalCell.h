#pragma once

#include <QChar>
#include <QColor>
#include <QString>

struct TerminalCell
{
    QString text = QString(QLatin1Char(' '));
    QChar character = QLatin1Char(' ');
    QColor foreground = QColor(204, 204, 204);
    QColor background = QColor(12, 12, 12);
    bool bold = false;
    bool dim = false;
    bool underline = false;
    bool inverse = false;
    bool wide = false;
    bool wideContinuation = false;
};
