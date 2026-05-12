#include "TerminalView.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>

namespace {
constexpr int MinTerminalColumns = 20;
constexpr int MinTerminalRows = 5;
const QColor BackgroundColor(8, 10, 15);
const QColor ForegroundColor(224, 234, 240);
const QColor SelectionBackgroundColor(38, 96, 130);
const QColor SelectionForegroundColor(255, 255, 255);
const QColor FocusBorderColor(73, 101, 128);
const QColor BorderColor(44, 48, 63);
const QColor GridGlowColor(18, 22, 32);
const QColor CornerGlowColor(50, 118, 160, 85);
}

TerminalView::TerminalView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(nullptr);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet(QStringLiteral(
        "QAbstractScrollArea { background: #080a0f; border: 1px solid #2c3040; border-radius: 9px; }"
        "QScrollBar:vertical { width: 10px; background: #0d1018; border-left: 1px solid #1f2431; margin: 3px 2px 3px 2px; }"
        "QScrollBar::handle:vertical { min-height: 30px; border-radius: 5px; background: #3b4052; }"
        "QScrollBar::handle:vertical:hover { background: #52596d; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    ));
    QPalette palette = viewport()->palette();
    palette.setColor(QPalette::Base, BackgroundColor);
    palette.setColor(QPalette::Window, BackgroundColor);
    viewport()->setPalette(palette);
    viewport()->setAutoFillBackground(false);

    QFont font(QStringLiteral("Cascadia Mono"));
    if (!QFontDatabase().families().contains(QStringLiteral("Cascadia Mono"))) {
        font = QFont(QStringLiteral("Consolas"));
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setPointSize(11);
    setFont(font);
    updateMetrics();

    cursorTimer_.setInterval(qMax(640, QApplication::cursorFlashTime() + 120));
    connect(&cursorTimer_, &QTimer::timeout, this, [this] {
        cursorVisible_ = !cursorVisible_;
        viewport()->update();
    });
    cursorTimer_.start();
}

void TerminalView::setBuffer(TerminalBuffer *buffer)
{
    buffer_ = buffer;
    syncedColumns_ = 0;
    syncedRows_ = 0;
    clearSelection();
    syncTerminalSize();
    updateScrollBar(true);
    if (syncTerminalSize()) {
        updateScrollBar(true);
    }
    viewport()->update();
}

void TerminalView::refresh()
{
    const bool wasAtBottom = isAtBottom();
    bool sizeChanged = syncTerminalSize();
    updateScrollBar(wasAtBottom || sizeChanged);
    updateInputMethodCursor();
    if (syncTerminalSize()) {
        sizeChanged = true;
        updateScrollBar(true);
    }
    viewport()->update();
}

void TerminalView::syncSizeToPty()
{
    if (syncTerminalSize()) {
        updateScrollBar(true);
        updateInputMethodCursor();
        viewport()->update();
    }
}

int TerminalView::columnCount() const
{
    return qMax(MinTerminalColumns, viewport()->width() / qMax(1, cellWidth_));
}

int TerminalView::rowCount() const
{
    return qMax(MinTerminalRows, viewport()->height() / qMax(1, cellHeight_));
}

bool TerminalView::event(QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            const QString input = terminalKeySequence(keyEvent);
            if (!input.isEmpty()) {
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
                emit rawInputRequested(input);
                event->accept();
                return true;
            }
        }
    }

    return QAbstractScrollArea::event(event);
}

QVariant TerminalView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImHints:
        return int(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase | Qt::ImhPreferLowercase);
    case Qt::ImCursorRectangle:
        return inputMethodCursorRectangle();
    default:
        break;
    }
    return QAbstractScrollArea::inputMethodQuery(query);
}

QRect TerminalView::inputMethodCursorRectangle() const
{
    if (!buffer_) {
        return QRect(0, 0, cellWidth_, cellHeight_);
    }

    const int column = qBound(0, buffer_->cursorColumn() + preeditCursor_, columnCount() - 1);
    const int viewRow = buffer_->historyLineCount() + buffer_->cursorRow() - verticalScrollBar()->value();
    const int row = qBound(0, viewRow, qMax(0, visibleRowCount() - 1));
    const QPoint topLeft = viewport()->mapTo(this, QPoint(column * cellWidth_, row * cellHeight_));
    return QRect(topLeft, QSize(cellWidth_, cellHeight_));
}

void TerminalView::updateInputMethodCursor()
{
    if (hasFocus() && QGuiApplication::inputMethod()) {
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    }
}

void TerminalView::inputMethodEvent(QInputMethodEvent *event)
{
    preeditText_ = event->preeditString();
    preeditCursor_ = preeditText_.size();
    for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
        if (attribute.type == QInputMethodEvent::Cursor) {
            preeditCursor_ = attribute.start;
            break;
        }
    }

    if (!event->commitString().isEmpty()) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        emit rawInputRequested(inputWithCommandAliases(event->commitString()));
    }

    updateInputMethodCursor();
    viewport()->update();
    event->accept();
}

QString TerminalView::inputWithCommandAliases(const QString &input)
{
    if (input == QStringLiteral("\r")) {
        const QString line = currentInputLine_;
        const QString command = line.trimmed();
        currentInputLine_.clear();
        const QString compactCommand = command;
        QString dots;
        if (compactCommand.startsWith(QStringLiteral("cd "))) {
            dots = compactCommand.mid(3).trimmed();
        } else if (compactCommand.startsWith(QStringLiteral("cd"))) {
            dots = compactCommand.mid(2);
        }

        if (dots.size() >= 2) {
            bool dotsOnly = true;
            for (const QChar ch : dots) {
                if (ch != QLatin1Char('.')) {
                    dotsOnly = false;
                    break;
                }
            }
            if (dotsOnly) {
                QStringList parents;
                for (int i = 1; i < dots.size(); ++i) {
                    parents.append(QStringLiteral(".."));
                }
                return QString(line.size(), QLatin1Char('\b')) + QStringLiteral("cd ") + parents.join(QStringLiteral("\\")) + QStringLiteral("\r");
            }
        }
        return input;
    }

    if (input == QStringLiteral("\x7f") || input == QStringLiteral("\b")) {
        if (!currentInputLine_.isEmpty()) {
            currentInputLine_.chop(1);
        }
        return input;
    }

    if (input == QStringLiteral("\x03") || input == QStringLiteral("\x1b")) {
        currentInputLine_.clear();
        return input;
    }

    if (input.size() == 1 && input.at(0).category() != QChar::Other_Control) {
        currentInputLine_.append(input);
    }
    return input;
}

void TerminalView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        if (hasSelection()) {
            copySelectionToClipboard();
        } else {
            emit rawInputRequested(QStringLiteral("\x03"));
        }
        event->accept();
        return;
    }

    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        if (hasSelection()) {
            copySelectionToClipboard();
        }
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Paste)
        || (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_V)) {
        pasteClipboard();
        event->accept();
        return;
    }

    QString input = terminalKeySequence(event);
    if (input.isEmpty()) {
        input = event->text();
    }

    updateInputMethodCursor();
    if (!input.isEmpty()) {
        preeditText_.clear();
        preeditCursor_ = 0;
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        emit rawInputRequested(inputWithCommandAliases(input));
        event->accept();
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

void TerminalView::focusInEvent(QFocusEvent *event)
{
    cursorVisible_ = true;
    cursorTimer_.start();
    if (buffer_ && buffer_->focusEventReportingEnabled()) {
        emit rawInputRequested(QStringLiteral("\x1b[I"));
    }
    updateInputMethodCursor();
    viewport()->update();
    QAbstractScrollArea::focusInEvent(event);
}

void TerminalView::focusOutEvent(QFocusEvent *event)
{
    cursorTimer_.stop();
    cursorVisible_ = true;
    preeditText_.clear();
    preeditCursor_ = 0;
    if (buffer_ && buffer_->focusEventReportingEnabled()) {
        emit rawInputRequested(QStringLiteral("\x1b[O"));
    }
    viewport()->update();
    QAbstractScrollArea::focusOutEvent(event);
}

void TerminalView::mousePressEvent(QMouseEvent *event)
{
    setFocus();

    if (event->button() == Qt::RightButton) {
        if (hasSelection()) {
            copySelectionToClipboard();
        } else {
            pasteClipboard();
        }
        event->accept();
        return;
    }

    if (buffer_ && sendMouseEvent(event, false)) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && buffer_) {
        selecting_ = true;
        selectionAnchor_ = cellAtPosition(event->pos());
        selectionCurrent_ = selectionAnchor_;
        viewport()->update();
        event->accept();
        return;
    }

    QAbstractScrollArea::mousePressEvent(event);
}

void TerminalView::mouseMoveEvent(QMouseEvent *event)
{
    if (buffer_ && sendMouseEvent(event, false, true)) {
        event->accept();
        return;
    }

    if (selecting_ && buffer_) {
        selectionCurrent_ = cellAtPosition(event->pos());
        viewport()->update();
        event->accept();
        return;
    }

    QAbstractScrollArea::mouseMoveEvent(event);
}

void TerminalView::mouseReleaseEvent(QMouseEvent *event)
{
    if (buffer_ && sendMouseEvent(event, true)) {
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && selecting_) {
        selecting_ = false;
        if (!hasSelection()) {
            clearSelection();
        } else {
            viewport()->update();
        }
        event->accept();
        return;
    }

    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TerminalView::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(viewport()->rect(), BackgroundColor);
    painter.fillRect(QRect(0, 0, viewport()->width(), 1), GridGlowColor);
    painter.fillRect(QRect(0, viewport()->height() - 1, viewport()->width(), 1), QColor(4, 6, 10));
    painter.setPen(QPen(CornerGlowColor, 2));
    painter.drawLine(0, 0, qMin(72, viewport()->width()), 0);
    painter.drawLine(0, 0, 0, qMin(72, viewport()->height()));

    if (!buffer_) {
        return;
    }

    painter.setFont(font());
    const int firstLine = verticalScrollBar()->value();
    const int visibleRows = visibleRowCount();
    const int totalLines = buffer_->totalLineCount();

    for (int viewRow = 0; viewRow < visibleRows; ++viewRow) {
        const int logicalRow = firstLine + viewRow;
        if (logicalRow < 0 || logicalRow >= totalLines) {
            continue;
        }

        const auto &line = buffer_->lineAt(logicalRow);
        for (int column = 0; column < qMin(line.size(), columnCount()); ++column) {
            const TerminalCell &cell = line[column];
            if (cell.wideContinuation) {
                continue;
            }
            const int x = column * cellWidth_;
            const int y = viewRow * cellHeight_;
            const int width = cell.wide ? cellWidth_ * 2 : cellWidth_;
            const bool selected = isCellSelected(logicalRow, column) || (cell.wide && isCellSelected(logicalRow, column + 1));
            QColor foreground = cell.foreground.isValid() ? cell.foreground : ForegroundColor;
            QColor background = cell.background.isValid() ? cell.background : BackgroundColor;
            if (cell.inverse) {
                std::swap(foreground, background);
            }
            if (cell.dim) {
                foreground = QColor(foreground.red() * 2 / 3, foreground.green() * 2 / 3, foreground.blue() * 2 / 3);
            }
            if (selected) {
                foreground = SelectionForegroundColor;
                background = SelectionBackgroundColor;
            }

            if (background != BackgroundColor || selected) {
                painter.fillRect(QRect(x, y, width, cellHeight_), background);
            }
            if (cell.text == QString(QLatin1Char(' ')) || cell.text.isEmpty()) {
                continue;
            }

            QFont cellFont = font();
            cellFont.setBold(cell.bold);
            painter.setFont(cellFont);
            painter.setPen(foreground);
            painter.drawText(QRect(x, y, width, cellHeight_), Qt::AlignLeft | Qt::AlignVCenter, cell.text);
            if (cell.underline) {
                painter.drawLine(x, y + baseline_ + 1, x + width - 1, y + baseline_ + 1);
            }
        }
    }

    painter.setFont(font());
    const int cursorLogicalRow = buffer_->historyLineCount() + buffer_->cursorRow();
    const int cursorViewRow = cursorLogicalRow - firstLine;
    if (!preeditText_.isEmpty() && cursorViewRow >= 0 && cursorViewRow < visibleRows) {
        const int x = buffer_->cursorColumn() * cellWidth_;
        const int y = cursorViewRow * cellHeight_;
        const int width = qMax(cellWidth_, cellWidth_ * preeditText_.size());
        painter.fillRect(QRect(x, y, width, cellHeight_), BackgroundColor);
        painter.setPen(ForegroundColor);
        painter.drawText(QRect(x, y, width, cellHeight_), Qt::AlignLeft | Qt::AlignVCenter, preeditText_);
        painter.drawLine(x, y + cellHeight_ - 2, x + width - 1, y + cellHeight_ - 2);
    }
    if (cursorViewRow >= 0 && cursorViewRow < visibleRows && buffer_->cursorVisible() && preeditText_.isEmpty() && (!buffer_->cursorBlink() || (hasFocus() ? cursorVisible_ : true))) {
        const int x = buffer_->cursorColumn() * cellWidth_;
        const int y = cursorViewRow * cellHeight_;
        QRect cursorRect(x, y, cellWidth_, cellHeight_);
        if (buffer_->cursorShape() == TerminalBuffer::CursorShape::Underline) {
            cursorRect = QRect(x, y + cellHeight_ - 2, cellWidth_, 1);
        } else if (buffer_->cursorShape() == TerminalBuffer::CursorShape::Bar) {
            cursorRect = QRect(x, y + 1, 1, cellHeight_ - 2);
        }
        if (hasFocus()) {
            painter.fillRect(cursorRect, ForegroundColor);
            if (buffer_->cursorShape() == TerminalBuffer::CursorShape::Block) {
                const auto &line = buffer_->lineAt(cursorLogicalRow);
                if (buffer_->cursorColumn() < line.size() && !line[buffer_->cursorColumn()].wideContinuation && line[buffer_->cursorColumn()].text != QString(QLatin1Char(' ')) && !line[buffer_->cursorColumn()].text.isEmpty()) {
                    const TerminalCell &cell = line[buffer_->cursorColumn()];
                    QFont cellFont = font();
                    cellFont.setBold(cell.bold);
                    painter.setFont(cellFont);
                    painter.setPen(BackgroundColor);
                    painter.drawText(QRect(x, y, cell.wide ? cellWidth_ * 2 : cellWidth_, cellHeight_), Qt::AlignLeft | Qt::AlignVCenter, cell.text);
                    painter.setFont(font());
                }
            }
        } else {
            painter.setPen(ForegroundColor);
            if (buffer_->cursorShape() == TerminalBuffer::CursorShape::Block) {
                painter.drawRect(cursorRect.adjusted(0, 0, -1, -1));
            } else {
                painter.fillRect(cursorRect, ForegroundColor);
            }
        }
    }

    painter.setPen(hasFocus() ? FocusBorderColor : BorderColor);
    painter.drawRect(viewport()->rect().adjusted(0, 0, -1, -1));
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateMetrics();
    syncTerminalSize();
    updateScrollBar(true);
    updateInputMethodCursor();
    if (syncTerminalSize()) {
        updateScrollBar(true);
    }
}

void TerminalView::wheelEvent(QWheelEvent *event)
{
    if (buffer_ && sendWheelEvent(event)) {
        event->accept();
        return;
    }

    QAbstractScrollArea::wheelEvent(event);
    viewport()->update();
}

QString TerminalView::terminalKeySequence(QKeyEvent *event) const
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    const QString mod = modifierParameter(modifiers);
    QString sequence;

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        sequence = QStringLiteral("\r");
        break;
    case Qt::Key_Backspace:
        sequence = alt ? QStringLiteral("\x1b\x7f") : QStringLiteral("\x7f");
        break;
    case Qt::Key_Tab:
        sequence = shift ? QStringLiteral("\x1b[Z") : QStringLiteral("\t");
        break;
    case Qt::Key_Backtab:
        sequence = QStringLiteral("\x1b[Z");
        break;
    case Qt::Key_Escape:
        sequence = QStringLiteral("\x1b");
        break;
    case Qt::Key_Left:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[D") : QStringLiteral("\x1b[1;%1D").arg(mod);
        break;
    case Qt::Key_Right:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[C") : QStringLiteral("\x1b[1;%1C").arg(mod);
        break;
    case Qt::Key_Up:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[A") : QStringLiteral("\x1b[1;%1A").arg(mod);
        break;
    case Qt::Key_Down:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[B") : QStringLiteral("\x1b[1;%1B").arg(mod);
        break;
    case Qt::Key_Home:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[H") : QStringLiteral("\x1b[1;%1H").arg(mod);
        break;
    case Qt::Key_End:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[F") : QStringLiteral("\x1b[1;%1F").arg(mod);
        break;
    case Qt::Key_Insert:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[2~") : QStringLiteral("\x1b[2;%1~").arg(mod);
        break;
    case Qt::Key_Delete:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[3~") : QStringLiteral("\x1b[3;%1~").arg(mod);
        break;
    case Qt::Key_PageUp:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[5~") : QStringLiteral("\x1b[5;%1~").arg(mod);
        break;
    case Qt::Key_PageDown:
        sequence = mod.isEmpty() ? QStringLiteral("\x1b[6~") : QStringLiteral("\x1b[6;%1~").arg(mod);
        break;
    default:
        break;
    }

    if (sequence.isEmpty()) {
        sequence = modifiedPrintableKeySequence(event);
    }

    if (sequence.isEmpty() && ctrl && !alt) {
        const int key = event->key();
        if (key >= Qt::Key_A && key <= Qt::Key_Z) {
            const int codepoint = shift ? ('A' + (key - Qt::Key_A)) : ('a' + (key - Qt::Key_A));
            const QString extended = extendedKeySequence(codepoint, mod);
            if (!extended.isEmpty()) {
                sequence = extended;
            } else {
                const ushort controlCode = static_cast<ushort>(key - Qt::Key_A + 1);
                sequence = QString(QChar(controlCode));
            }
        } else if (key == Qt::Key_BracketLeft || key == Qt::Key_3) {
            sequence = QStringLiteral("\x1b");
        } else if (key == Qt::Key_Backslash || key == Qt::Key_4) {
            sequence = QString(QChar(0x1c));
        } else if (key == Qt::Key_BracketRight || key == Qt::Key_5) {
            sequence = QString(QChar(0x1d));
        } else if (key == Qt::Key_6) {
            sequence = QString(QChar(0x1e));
        } else if (key == Qt::Key_Minus || key == Qt::Key_Underscore) {
            sequence = QString(QChar(0x1f));
        }
    }

    if (sequence.isEmpty() && alt && !event->text().isEmpty()) {
        sequence = QStringLiteral("\x1b") + event->text();
    }

    return sequence;
}

QString TerminalView::modifierParameter(Qt::KeyboardModifiers modifiers) const
{
    int value = 1;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        value += 1;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        value += 2;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        value += 4;
    }

    if (value == 1) {
        return QString();
    }
    return QString::number(value);
}

QString TerminalView::extendedKeySequence(int codepoint, const QString &modifier) const
{
    if (!buffer_ || modifier.isEmpty()) {
        return QString();
    }

    if (buffer_->kittyKeyboardEnabled()) {
        return QStringLiteral("\x1b[%1;%2u").arg(codepoint).arg(modifier);
    }

    if (buffer_->modifyOtherKeysMode() > 0) {
        return QStringLiteral("\x1b[27;%1;%2~").arg(modifier).arg(codepoint);
    }

    return QString();
}

QString TerminalView::modifiedPrintableKeySequence(QKeyEvent *event) const
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    if (!ctrl && !alt) {
        return QString();
    }

    const QString text = event->text();
    if (text.size() != 1 || text.at(0).unicode() < 0x20) {
        return QString();
    }

    const int codepoint = text.at(0).unicode();
    const QString mod = modifierParameter(modifiers);
    if (mod.isEmpty()) {
        return QString();
    }

    return extendedKeySequence(codepoint, mod);
}

void TerminalView::updateMetrics()
{
    const QFontMetrics metrics(font());
    cellWidth_ = qMax(1, metrics.horizontalAdvance(QLatin1Char('W')));
    cellHeight_ = qMax(1, metrics.height());
    baseline_ = metrics.ascent();
}

bool TerminalView::syncTerminalSize()
{
    if (!buffer_) {
        syncedColumns_ = 0;
        syncedRows_ = 0;
        return false;
    }

    buffer_->setCellSize(cellWidth_, cellHeight_);
    const int columns = columnCount();
    const int rows = rowCount();
    if (columns == syncedColumns_ && rows == syncedRows_) {
        return false;
    }

    syncedColumns_ = columns;
    syncedRows_ = rows;
    buffer_->resize(columns, rows);
    emit terminalSizeChanged(columns, rows);
    return true;
}

void TerminalView::updateScrollBar(bool stickToBottom)
{
    if (!buffer_) {
        verticalScrollBar()->setRange(0, 0);
        return;
    }

    const int maxValue = qMax(0, buffer_->totalLineCount() - visibleRowCount());
    verticalScrollBar()->setPageStep(visibleRowCount());
    verticalScrollBar()->setRange(0, maxValue);
    if (stickToBottom) {
        verticalScrollBar()->setValue(maxValue);
    }
}

bool TerminalView::isAtBottom() const
{
    return verticalScrollBar()->value() == verticalScrollBar()->maximum();
}

int TerminalView::visibleRowCount() const
{
    return qMax(1, viewport()->height() / qMax(1, cellHeight_));
}

QPoint TerminalView::cellAtPosition(const QPoint &position) const
{
    const int totalLines = buffer_ ? buffer_->totalLineCount() : 0;
    const int row = qBound(0, verticalScrollBar()->value() + position.y() / qMax(1, cellHeight_), qMax(0, totalLines - 1));
    const int column = qBound(0, position.x() / qMax(1, cellWidth_), qMax(0, columnCount() - 1));
    return QPoint(column, row);
}

int TerminalView::normalizedSelectionStart() const
{
    const int anchor = selectionAnchor_.y() * columnCount() + selectionAnchor_.x();
    const int current = selectionCurrent_.y() * columnCount() + selectionCurrent_.x();
    return qMin(anchor, current);
}

int TerminalView::normalizedSelectionEnd() const
{
    const int anchor = selectionAnchor_.y() * columnCount() + selectionAnchor_.x();
    const int current = selectionCurrent_.y() * columnCount() + selectionCurrent_.x();
    return qMax(anchor, current);
}

bool TerminalView::hasSelection() const
{
    return buffer_ && selectionAnchor_ != selectionCurrent_;
}

bool TerminalView::isCellSelected(int logicalRow, int column) const
{
    if (!hasSelection()) {
        return false;
    }

    const int index = logicalRow * columnCount() + column;
    return index >= normalizedSelectionStart() && index <= normalizedSelectionEnd();
}

QString TerminalView::selectedText() const
{
    if (!hasSelection()) {
        return QString();
    }

    QString text;
    const int start = normalizedSelectionStart();
    const int end = normalizedSelectionEnd();
    const int columns = columnCount();
    const int startRow = start / columns;
    const int endRow = end / columns;
    const int startColumn = start % columns;
    const int endColumn = end % columns;

    for (int row = startRow; row <= endRow; ++row) {
        if (row < 0 || row >= buffer_->totalLineCount()) {
            continue;
        }

        const auto &line = buffer_->lineAt(row);
        const int fromColumn = row == startRow ? startColumn : 0;
        const int toColumn = row == endRow ? endColumn : qMin(columns - 1, line.size() - 1);
        QString lineText;
        for (int column = fromColumn; column <= toColumn && column < line.size(); ++column) {
            const TerminalCell &cell = line[column];
            if (cell.wideContinuation) {
                continue;
            }
            lineText.append(cell.text);
        }

        while (lineText.endsWith(QLatin1Char(' '))) {
            lineText.chop(1);
        }

        text.append(lineText);
        if (row != endRow) {
            text.append(QLatin1Char('\n'));
        }
    }

    return text;
}

bool TerminalView::copySelection()
{
    if (!hasSelection()) {
        emit interactionMessage(QStringLiteral("没有选中的内容可复制"));
        return false;
    }

    QGuiApplication::clipboard()->setText(selectedText());
    clearSelection();
    emit interactionMessage(QStringLiteral("已复制选中文本"));
    return true;
}

bool TerminalView::pasteFromClipboard()
{
    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty()) {
        emit interactionMessage(QStringLiteral("剪贴板为空"));
        return false;
    }

    if (buffer_ && buffer_->bracketedPasteEnabled()) {
        text = QStringLiteral("\x1b[200~") + text + QStringLiteral("\x1b[201~");
    } else {
        text.replace(QStringLiteral("\n"), QStringLiteral("\r"));
    }

    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    emit rawInputRequested(text);
    emit interactionMessage(QStringLiteral("已发送剪贴板内容"));
    return true;
}

void TerminalView::copySelectionToClipboard()
{
    copySelection();
}

void TerminalView::pasteClipboard()
{
    pasteFromClipboard();
}

void TerminalView::clearSelection()
{
    selectionAnchor_ = QPoint();
    selectionCurrent_ = QPoint();
    selecting_ = false;
    viewport()->update();
}

bool TerminalView::sendMouseEvent(QMouseEvent *event, bool release, bool drag)
{
    if (!buffer_ || buffer_->mouseTrackingMode() == 0) {
        return false;
    }

    const QPoint cell = cellAtPosition(event->pos());
    const int row = cell.y() - buffer_->historyLineCount() + 1;
    const int column = cell.x() + 1;
    if (row < 1 || row > buffer_->rows() || column < 1 || column > buffer_->columns()) {
        return true;
    }

    int button = 0;
    if (release) {
        button = pressedMouseButton_ >= 0 ? pressedMouseButton_ : 0;
        pressedMouseButton_ = -1;
    } else if (drag && pressedMouseButton_ >= 0) {
        button = pressedMouseButton_ + 32;
    } else {
        if (event->button() == Qt::RightButton) {
            button = 2;
        } else if (event->button() == Qt::MiddleButton) {
            button = 1;
        }
        pressedMouseButton_ = button;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        button += 4;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        button += 8;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        button += 16;
    }

    if (drag && buffer_->mouseTrackingMode() < 1002) {
        return true;
    }

    emit rawInputRequested(mouseReport(button, column, row, release));
    return true;
}

bool TerminalView::sendWheelEvent(QWheelEvent *event)
{
    if (!buffer_ || buffer_->mouseTrackingMode() == 0) {
        return false;
    }

    const QPoint cell = cellAtPosition(event->pos());
    const int row = cell.y() - buffer_->historyLineCount() + 1;
    const int column = cell.x() + 1;
    if (row < 1 || row > buffer_->rows() || column < 1 || column > buffer_->columns()) {
        return true;
    }

    const int button = event->angleDelta().y() >= 0 ? 64 : 65;
    emit rawInputRequested(mouseReport(button, column, row, false));
    return true;
}

QString TerminalView::mouseReport(int button, int column, int row, bool release) const
{
    if (buffer_ && buffer_->sgrMouseEnabled()) {
        return QStringLiteral("\x1b[<%1;%2;%3%4")
            .arg(button)
            .arg(column)
            .arg(row)
            .arg(release ? QLatin1Char('m') : QLatin1Char('M'));
    }

    const int code = release ? 3 : button;
    return QStringLiteral("\x1b[M%1%2%3")
        .arg(QChar(32 + code))
        .arg(QChar(32 + column))
        .arg(QChar(32 + row));
}
