#include "TerminalEmulator.h"

#include <QStringList>
#include <QVector>

namespace {
QString cleanedParams(QString params)
{
    while (!params.isEmpty()) {
        const QChar ch = params.front();
        if ((ch.unicode() >= 0x30 && ch.unicode() <= 0x3f) || ch == QLatin1Char(';')) {
            break;
        }
        params.remove(0, 1);
    }
    return params;
}
}

TerminalEmulator::TerminalEmulator(QObject *parent)
    : QObject(parent)
{
}

void TerminalEmulator::reset(TerminalBuffer *buffer)
{
    buffer_ = buffer;
    pendingText_.clear();
    pendingEscape_.clear();
    inEscape_ = false;
    inCsi_ = false;
    inOsc_ = false;
    oscText_.clear();
    lineDrawingCharset_ = false;
    csiParams_.clear();
    currentAttributes_ = TerminalCell();
}

void TerminalEmulator::process(const QString &text)
{
    if (!buffer_) {
        return;
    }

    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);

        if (inOsc_) {
            if (ch == QLatin1Char('\a')) {
                handleOsc(oscText_);
                oscText_.clear();
                inOsc_ = false;
            } else if (ch == QLatin1Char('\x1b') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\\')) {
                handleOsc(oscText_);
                oscText_.clear();
                inOsc_ = false;
                ++i;
            } else {
                oscText_.append(ch);
            }
            continue;
        }

        if (inCsi_) {
            if (ch.unicode() >= 0x40 && ch.unicode() <= 0x7e) {
                handleCsi(csiParams_, ch);
                inCsi_ = false;
                csiParams_.clear();
            } else {
                csiParams_.append(ch);
            }
            continue;
        }

        if (inEscape_) {
            inEscape_ = false;
            if (ch == QLatin1Char('[')) {
                inCsi_ = true;
                csiParams_.clear();
                continue;
            }
            if (ch == QLatin1Char(']')) {
                inOsc_ = true;
                oscText_.clear();
                continue;
            }
            if (ch == QLatin1Char('(') && i + 1 < text.size()) {
                lineDrawingCharset_ = text.at(++i) == QLatin1Char('0');
                continue;
            }
            if (ch == QLatin1Char(')') && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (ch == QLatin1Char('7')) {
                buffer_->saveCursor();
                continue;
            }
            if (ch == QLatin1Char('8')) {
                buffer_->restoreCursor();
                continue;
            }
            if (ch == QLatin1Char('D')) {
                buffer_->lineFeed();
                continue;
            }
            if (ch == QLatin1Char('E')) {
                buffer_->carriageReturn();
                buffer_->lineFeed();
                continue;
            }
            if (ch == QLatin1Char('M')) {
                buffer_->reverseIndex();
                continue;
            }
            if (ch == QLatin1Char('c')) {
                buffer_->clearScreen();
                buffer_->resetScrollRegion();
                continue;
            }
            continue;
        }

        if (ch == QLatin1Char('\x1b')) {
            inEscape_ = true;
            continue;
        }

        switch (ch.unicode()) {
        case '\r':
            buffer_->carriageReturn();
            break;
        case '\n':
            buffer_->lineFeed();
            break;
        case '\b':
            buffer_->backspace();
            break;
        case '\t':
            buffer_->tab();
            break;
        default:
            if (ch.unicode() >= 0x20) {
                if (ch.unicode() >= 0xd800 && ch.unicode() <= 0xdbff && i + 1 < text.size()) {
                    const QChar next = text.at(i + 1);
                    if (next.unicode() >= 0xdc00 && next.unicode() <= 0xdfff) {
                        buffer_->putText(QString(ch) + next, currentAttributes_);
                        ++i;
                        break;
                    }
                }
                buffer_->putChar(mapCharacter(ch), currentAttributes_);
            }
            break;
        }
    }
}

void TerminalEmulator::flushText()
{
}

void TerminalEmulator::handleEscape()
{
}

void TerminalEmulator::handleOsc(const QString &text)
{
    const int separator = text.indexOf(QLatin1Char(';'));
    if (separator <= 0) {
        return;
    }

    const int command = text.left(separator).toInt();
    if (command == 0 || command == 2) {
        emit titleChanged(text.mid(separator + 1));
    }
}

QChar TerminalEmulator::mapCharacter(QChar ch) const
{
    if (!lineDrawingCharset_) {
        return ch;
    }

    switch (ch.toLatin1()) {
    case 'j': return QChar(0x2518);
    case 'k': return QChar(0x2510);
    case 'l': return QChar(0x250c);
    case 'm': return QChar(0x2514);
    case 'n': return QChar(0x253c);
    case 'q': return QChar(0x2500);
    case 't': return QChar(0x251c);
    case 'u': return QChar(0x2524);
    case 'v': return QChar(0x2534);
    case 'w': return QChar(0x252c);
    case 'x': return QChar(0x2502);
    default: return ch;
    }
}

void TerminalEmulator::handleSgr(const QStringList &parts)
{
    const QStringList params = parts.isEmpty() ? QStringList{QStringLiteral("0")} : parts;
    for (int i = 0; i < params.size(); ++i) {
        const int value = params[i].isEmpty() ? 0 : params[i].toInt();
        switch (value) {
        case 0:
            currentAttributes_ = TerminalCell();
            break;
        case 1:
            currentAttributes_.bold = true;
            currentAttributes_.dim = false;
            break;
        case 2:
            currentAttributes_.dim = true;
            break;
        case 4:
            currentAttributes_.underline = true;
            break;
        case 7:
            currentAttributes_.inverse = true;
            break;
        case 22:
            currentAttributes_.bold = false;
            currentAttributes_.dim = false;
            break;
        case 24:
            currentAttributes_.underline = false;
            break;
        case 27:
            currentAttributes_.inverse = false;
            break;
        case 39:
            currentAttributes_.foreground = TerminalCell().foreground;
            break;
        case 49:
            currentAttributes_.background = TerminalCell().background;
            break;
        default:
            if (value >= 30 && value <= 37) {
                currentAttributes_.foreground = colorFromAnsiIndex(value - 30);
            } else if (value >= 40 && value <= 47) {
                currentAttributes_.background = colorFromAnsiIndex(value - 40);
            } else if (value >= 90 && value <= 97) {
                currentAttributes_.foreground = colorFromAnsiIndex(value - 90 + 8);
            } else if (value >= 100 && value <= 107) {
                currentAttributes_.background = colorFromAnsiIndex(value - 100 + 8);
            } else if ((value == 38 || value == 48) && i + 1 < params.size()) {
                QColor color;
                const int mode = params[++i].toInt();
                if (mode == 5 && i + 1 < params.size()) {
                    color = colorFrom256Index(params[++i].toInt());
                } else if (mode == 2 && i + 3 < params.size()) {
                    const int red = params[++i].toInt();
                    const int green = params[++i].toInt();
                    const int blue = params[++i].toInt();
                    color = QColor(red, green, blue);
                }
                if (color.isValid()) {
                    if (value == 38) {
                        currentAttributes_.foreground = color;
                    } else {
                        currentAttributes_.background = color;
                    }
                }
            }
            break;
        }
    }
}

QColor TerminalEmulator::colorFromAnsiIndex(int index) const
{
    static const QVector<QColor> colors = {
        QColor(12, 12, 12),
        QColor(197, 15, 31),
        QColor(19, 161, 14),
        QColor(193, 156, 0),
        QColor(0, 55, 218),
        QColor(136, 23, 152),
        QColor(58, 150, 221),
        QColor(204, 204, 204),
        QColor(118, 118, 118),
        QColor(231, 72, 86),
        QColor(22, 198, 12),
        QColor(249, 241, 165),
        QColor(59, 120, 255),
        QColor(180, 0, 158),
        QColor(97, 214, 214),
        QColor(242, 242, 242),
    };
    return colors[qBound(0, index, colors.size() - 1)];
}

QColor TerminalEmulator::colorFrom256Index(int index) const
{
    index = qBound(0, index, 255);
    if (index < 16) {
        return colorFromAnsiIndex(index);
    }
    if (index >= 232) {
        const int level = 8 + (index - 232) * 10;
        return QColor(level, level, level);
    }

    const int color = index - 16;
    const int red = color / 36;
    const int green = (color / 6) % 6;
    const int blue = color % 6;
    auto component = [](int value) {
        return value == 0 ? 0 : 55 + value * 40;
    };
    return QColor(component(red), component(green), component(blue));
}

void TerminalEmulator::handlePrivateMode(const QString &params, bool enabled)
{
    const QStringList modes = params.mid(1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &mode : modes) {
        const int value = mode.toInt();
        switch (value) {
        case 6:
            buffer_->setOriginMode(enabled);
            break;
        case 7:
            buffer_->setAutoWrapEnabled(enabled);
            break;
        case 25:
            buffer_->setCursorVisible(enabled);
            break;
        case 1000:
            buffer_->setMouseTrackingMode(enabled ? 1000 : 0);
            break;
        case 1002:
            buffer_->setMouseTrackingMode(enabled ? 1002 : 0);
            break;
        case 1003:
            buffer_->setMouseTrackingMode(enabled ? 1003 : 0);
            break;
        case 1006:
            buffer_->setSgrMouseEnabled(enabled);
            break;
        case 1004:
            buffer_->setFocusEventReportingEnabled(enabled);
            break;
        case 1047:
        case 1049:
            buffer_->useAlternateScreen(enabled);
            break;
        case 2004:
            buffer_->setBracketedPasteEnabled(enabled);
            break;
        default:
            break;
        }
    }
}

void TerminalEmulator::handleKeyModifierOptions(const QString &params)
{
    const QString normalizedParams = cleanedParams(params);
    const QStringList parts = normalizedParams.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    const int resource = parts.size() > 0 && !parts[0].isEmpty() ? parts[0].toInt() : 0;
    const int value = parts.size() > 1 && !parts[1].isEmpty() ? parts[1].toInt() : 0;
    if (resource == 4) {
        buffer_->setModifyOtherKeysMode(value);
    }
}

void TerminalEmulator::queryKeyModifierOptions(const QString &params)
{
    const QString normalizedParams = cleanedParams(params);
    const QStringList parts = normalizedParams.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    const int resource = parts.size() > 0 && !parts[0].isEmpty() ? parts[0].toInt() : 0;
    if (resource == 4) {
        emit responseRequested(QStringLiteral("\x1b[>4;%1m").arg(buffer_->modifyOtherKeysMode()));
    }
}

void TerminalEmulator::handleKittyKeyboardProtocol(const QString &params)
{
    const QString normalizedParams = cleanedParams(params);
    const QStringList parts = normalizedParams.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    const int flags = parts.size() > 0 && !parts[0].isEmpty() ? parts[0].toInt() : 0;
    buffer_->setKittyKeyboardFlags(flags);
}

void TerminalEmulator::queryKittyKeyboardProtocol()
{
    if (!buffer_) {
        return;
    }

    emit responseRequested(QStringLiteral("\x1b[>1;0u"));
}

void TerminalEmulator::handleCursorStyle(int value)
{
    switch (value) {
    case 0:
    case 1:
        buffer_->setCursorShape(TerminalBuffer::CursorShape::Block);
        buffer_->setCursorBlink(true);
        break;
    case 2:
        buffer_->setCursorShape(TerminalBuffer::CursorShape::Block);
        buffer_->setCursorBlink(false);
        break;
    case 3:
        buffer_->setCursorShape(TerminalBuffer::CursorShape::Underline);
        buffer_->setCursorBlink(true);
        break;
    case 4:
        buffer_->setCursorShape(TerminalBuffer::CursorShape::Underline);
        buffer_->setCursorBlink(false);
        break;
    case 5:
        buffer_->setCursorShape(TerminalBuffer::CursorShape::Bar);
        buffer_->setCursorBlink(true);
        break;
    case 6:
        buffer_->setCursorShape(TerminalBuffer::CursorShape::Bar);
        buffer_->setCursorBlink(false);
        break;
    default:
        break;
    }
}

void TerminalEmulator::sendDeviceStatusReport(int value)
{
    if (!buffer_) {
        return;
    }

    if (value == 5) {
        emit responseRequested(QStringLiteral("\x1b[0n"));
    } else if (value == 6) {
        emit responseRequested(QStringLiteral("\x1b[%1;%2R").arg(buffer_->cursorRow() + 1).arg(buffer_->cursorColumnRaw() + 1));
    }
}

void TerminalEmulator::sendWindowReport(int value)
{
    if (!buffer_) {
        return;
    }

    switch (value) {
    case 14:
        emit responseRequested(QStringLiteral("\x1b[4;%1;%2t").arg(buffer_->cellHeight() * buffer_->rows()).arg(buffer_->cellWidth() * buffer_->columns()));
        break;
    case 16:
        emit responseRequested(QStringLiteral("\x1b[6;%1;%2t").arg(buffer_->cellHeight()).arg(buffer_->cellWidth()));
        break;
    case 18:
        emit responseRequested(QStringLiteral("\x1b[8;%1;%2t").arg(buffer_->rows()).arg(buffer_->columns()));
        break;
    case 19:
        emit responseRequested(QStringLiteral("\x1b[9;%1;%2t").arg(buffer_->cellHeight() * buffer_->rows()).arg(buffer_->cellWidth() * buffer_->columns()));
        break;
    default:
        break;
    }
}

void TerminalEmulator::handleCsi(const QString &params, QChar finalChar)
{
    if ((finalChar == QLatin1Char('h') || finalChar == QLatin1Char('l')) && params.startsWith(QLatin1Char('?'))) {
        handlePrivateMode(params, finalChar == QLatin1Char('h'));
        return;
    }

    const QString normalizedParams = cleanedParams(params);
    const QStringList parts = normalizedParams.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    auto param = [&](int index, int defaultValue) {
        if (index >= parts.size() || parts[index].isEmpty()) {
            return defaultValue;
        }
        return parts[index].toInt();
    };

    switch (finalChar.unicode()) {
    case 'A':
        buffer_->moveCursorRelative(-param(0, 1), 0);
        break;
    case 'B':
        buffer_->moveCursorRelative(param(0, 1), 0);
        break;
    case 'C':
        buffer_->moveCursorRelative(0, param(0, 1));
        break;
    case 'D':
        buffer_->moveCursorRelative(0, -param(0, 1));
        break;
    case 'E':
        buffer_->moveCursorNextLine(param(0, 1));
        break;
    case 'F':
        buffer_->moveCursorPreviousLine(param(0, 1));
        break;
    case 'H':
    case 'f':
        buffer_->moveCursorTo(param(0, 1) - 1, param(1, 1) - 1);
        break;
    case 'G':
        buffer_->moveCursorColumn(param(0, 1) - 1);
        break;
    case 'L':
        buffer_->insertLines(param(0, 1));
        break;
    case 'M':
        buffer_->deleteLines(param(0, 1));
        break;
    case 'P':
        buffer_->deleteCharacters(param(0, 1));
        break;
    case 'X':
        buffer_->eraseCharacters(param(0, 1));
        break;
    case 'J':
        buffer_->clearScreenMode(param(0, 0));
        break;
    case 'K':
        buffer_->clearLine(param(0, 0));
        break;
    case 'S':
        buffer_->scrollUpLines(param(0, 1));
        break;
    case 'T':
        buffer_->scrollDownLines(param(0, 1));
        break;
    case '`':
        buffer_->moveCursorColumn(param(0, 1) - 1);
        break;
    case 'a':
        buffer_->moveCursorRelative(0, param(0, 1));
        break;
    case 'd':
        buffer_->moveCursorRow(param(0, 1) - 1);
        break;
    case 'q':
        if (params.endsWith(QLatin1Char(' ')) || params.startsWith(QLatin1Char(' '))) {
            handleCursorStyle(param(0, 0));
        }
        break;
    case 'r':
        buffer_->setScrollRegion(param(0, 1) - 1, param(1, buffer_->rows()) - 1);
        break;
    case 's':
        buffer_->saveCursor();
        break;
    case 'u':
        if (params.startsWith(QLatin1Char('?'))) {
            queryKittyKeyboardProtocol();
        } else if (params.isEmpty()) {
            buffer_->restoreCursor();
        } else {
            handleKittyKeyboardProtocol(params);
        }
        break;
    case 'c':
        if (params.startsWith(QLatin1Char('>'))) {
            emit responseRequested(QStringLiteral("\x1b[>0;10;1c"));
        } else {
            emit responseRequested(QStringLiteral("\x1b[?1;2c"));
        }
        break;
    case 'n':
        sendDeviceStatusReport(param(0, 0));
        break;
    case 't':
        sendWindowReport(param(0, 0));
        break;
    case 'm':
        if (params.startsWith(QLatin1Char('>'))) {
            handleKeyModifierOptions(params.mid(1));
        } else if (params.startsWith(QLatin1Char('?'))) {
            queryKeyModifierOptions(params.mid(1));
        } else {
            handleSgr(parts);
        }
        break;
    case '@':
        buffer_->insertCharacters(param(0, 1));
        break;
    default:
        break;
    }
}
