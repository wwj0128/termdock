#include "TerminalBuffer.h"

#include <algorithm>

TerminalBuffer::TerminalBuffer(int columns, int rows)
{
    resize(columns, rows);
}

void TerminalBuffer::resize(int columns, int rows)
{
    pendingWrap_ = false;
    const int oldColumns = columns_;
    const int oldRows = rows_;
    const int oldScrollTop = scrollTop_;
    const int oldScrollBottom = scrollBottom_;
    columns_ = std::max(20, columns);
    rows_ = std::max(5, rows);
    if (oldScrollTop == 0 && oldScrollBottom == oldRows - 1) {
        scrollTop_ = 0;
        scrollBottom_ = rows_ - 1;
    } else {
        scrollTop_ = std::min(oldScrollTop, rows_ - 1);
        scrollBottom_ = std::min(rows_ - 1, std::max(scrollTop_, oldScrollBottom + rows_ - oldRows));
    }

    QVector<Line> oldScreen = screen_;
    screen_.clear();
    screen_.reserve(rows_);
    resizeLines(mainScreen_, oldColumns);
    resizeLines(alternateScreen_, oldColumns);

    const int keepRows = std::min(rows_, oldScreen.size());
    const int start = std::max(0, oldScreen.size() - keepRows);
    for (int i = 0; i < keepRows; ++i) {
        Line line = oldScreen[start + i];
        line.resize(columns_);
        for (int column = oldColumns; column < columns_; ++column) {
            line[column] = TerminalCell();
        }
        for (int column = 0; column < columns_; ++column) {
            if (line[column].text.isEmpty() && !line[column].wideContinuation) {
                line[column] = TerminalCell();
            }
        }
        normalizeWideCells(line);
        screen_.append(line);
    }

    while (screen_.size() < rows_) {
        screen_.prepend(blankLine());
    }

    clampCursor();
}

void TerminalBuffer::clearScreen()
{
    pendingWrap_ = false;
    screen_.clear();
    for (int i = 0; i < rows_; ++i) {
        screen_.append(blankLine());
    }
    cursorRow_ = 0;
    cursorColumn_ = 0;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
}

void TerminalBuffer::clearAll()
{
    history_.clear();
    clearScreen();
}

void TerminalBuffer::clearLine(int mode)
{
    pendingWrap_ = false;
    if (screen_.isEmpty()) {
        return;
    }

    Line &line = screen_[cursorRow_];
    int start = 0;
    int end = columns_ - 1;
    if (mode == 0) {
        start = cursorColumn_;
    } else if (mode == 1) {
        end = cursorColumn_;
    }

    for (int column = start; column <= end && column < line.size(); ++column) {
        eraseCell(cursorRow_, column);
    }
}

void TerminalBuffer::clearScreenMode(int mode)
{
    pendingWrap_ = false;
    if (mode == 3) {
        history_.clear();
        return;
    }

    if (mode == 2) {
        for (int row = 0; row < rows_; ++row) {
            screen_[row] = blankLine();
        }
        return;
    }

    if (mode == 0) {
        clearLine(0);
        for (int row = cursorRow_ + 1; row < rows_; ++row) {
            screen_[row] = blankLine();
        }
        return;
    }

    if (mode == 1) {
        for (int row = 0; row < cursorRow_; ++row) {
            screen_[row] = blankLine();
        }
        clearLine(1);
    }
}

void TerminalBuffer::putChar(QChar ch, const TerminalCell &attributes)
{
    putText(QString(ch), attributes);
}

void TerminalBuffer::putText(const QString &text, const TerminalCell &attributes)
{
    if (text.isEmpty()) {
        return;
    }

    const int width = characterWidth(text);
    if (width == 0) {
        int targetColumn = cursorColumn_ - 1;
        if (targetColumn >= 0 && screen_[cursorRow_][targetColumn].wideContinuation) {
            --targetColumn;
        }
        if (targetColumn >= 0) {
            screen_[cursorRow_][targetColumn].text.append(text);
        }
        return;
    }

    if (pendingWrap_) {
        carriageReturn();
        lineFeed();
        pendingWrap_ = false;
    }

    int previousColumn = cursorColumn_ - 1;
    if (previousColumn >= 0 && screen_[cursorRow_][previousColumn].wideContinuation) {
        --previousColumn;
    }
    if (previousColumn >= 0 && screen_[cursorRow_][previousColumn].text.endsWith(QChar(0x200d))) {
        screen_[cursorRow_][previousColumn].text.append(text);
        return;
    }

    if (cursorColumn_ + width > columns_) {
        carriageReturn();
        lineFeed();
    }

    TerminalCell cell = attributes;
    cell.text = text;
    cell.character = text.at(0);
    cell.wide = width == 2;
    cell.wideContinuation = false;
    eraseCell(cursorRow_, cursorColumn_);
    if (width == 2 && cursorColumn_ + 1 < columns_) {
        eraseCell(cursorRow_, cursorColumn_ + 1);
    }
    screen_[cursorRow_][cursorColumn_] = cell;

    if (width == 2 && cursorColumn_ + 1 < columns_) {
        TerminalCell continuation;
        continuation.text.clear();
        continuation.wideContinuation = true;
        screen_[cursorRow_][cursorColumn_ + 1] = continuation;
    }

    cursorColumn_ += width;
    if (cursorColumn_ >= columns_) {
        cursorColumn_ = columns_ - 1;
        pendingWrap_ = autoWrapEnabled_;
    }
}

void TerminalBuffer::carriageReturn()
{
    pendingWrap_ = false;
    cursorColumn_ = 0;
}

void TerminalBuffer::lineFeed()
{
    pendingWrap_ = false;
    if (cursorRow_ == scrollBottom_) {
        scrollUpLines(1);
    } else if (cursorRow_ < rows_ - 1) {
        ++cursorRow_;
    }
}

void TerminalBuffer::reverseIndex()
{
    pendingWrap_ = false;
    if (cursorRow_ == scrollTop_) {
        scrollDownLines(1);
    } else if (cursorRow_ > 0) {
        --cursorRow_;
    }
}

void TerminalBuffer::backspace()
{
    pendingWrap_ = false;
    if (cursorColumn_ <= 0) {
        return;
    }

    --cursorColumn_;
    if (cursorColumn_ > 0 && screen_[cursorRow_][cursorColumn_].wideContinuation) {
        --cursorColumn_;
    }
}

void TerminalBuffer::tab()
{
    pendingWrap_ = false;
    const int nextStop = ((cursorColumn_ / 8) + 1) * 8;
    cursorColumn_ = std::min(columns_ - 1, nextStop);
}

void TerminalBuffer::moveCursorRelative(int rowDelta, int columnDelta)
{
    pendingWrap_ = false;
    cursorRow_ += rowDelta;
    cursorColumn_ += columnDelta;
    clampCursor();
}

void TerminalBuffer::moveCursorTo(int row, int column)
{
    pendingWrap_ = false;
    cursorRow_ = originMode_ ? scrollTop_ + row : row;
    cursorColumn_ = column;
    clampCursor();
}

void TerminalBuffer::moveCursorColumn(int column)
{
    pendingWrap_ = false;
    cursorColumn_ = column;
    clampCursor();
}

void TerminalBuffer::moveCursorRow(int row)
{
    pendingWrap_ = false;
    cursorRow_ = originMode_ ? scrollTop_ + row : row;
    clampCursor();
}

void TerminalBuffer::moveCursorNextLine(int count)
{
    pendingWrap_ = false;
    cursorRow_ += std::max(1, count);
    cursorColumn_ = 0;
    clampCursor();
}

void TerminalBuffer::moveCursorPreviousLine(int count)
{
    pendingWrap_ = false;
    cursorRow_ -= std::max(1, count);
    cursorColumn_ = 0;
    clampCursor();
}

void TerminalBuffer::saveCursor()
{
    savedCursorRow_ = cursorRow_;
    savedCursorColumn_ = cursorColumn_;
}

void TerminalBuffer::restoreCursor()
{
    pendingWrap_ = false;
    cursorRow_ = savedCursorRow_;
    cursorColumn_ = savedCursorColumn_;
    clampCursor();
}

void TerminalBuffer::eraseCharacters(int count)
{
    pendingWrap_ = false;
    const int end = std::min(columns_ - 1, cursorColumn_ + std::max(1, count) - 1);
    for (int column = cursorColumn_; column <= end; ++column) {
        eraseCell(cursorRow_, column);
    }
}

void TerminalBuffer::insertCharacters(int count)
{
    pendingWrap_ = false;
    Line &line = screen_[cursorRow_];
    const int amount = std::min(std::max(1, count), columns_ - cursorColumn_);
    for (int column = columns_ - 1; column >= cursorColumn_ + amount; --column) {
        line[column] = line[column - amount];
    }
    for (int column = cursorColumn_; column < cursorColumn_ + amount; ++column) {
        line[column] = TerminalCell();
    }
    normalizeWideCells(line);
}

void TerminalBuffer::deleteCharacters(int count)
{
    pendingWrap_ = false;
    Line &line = screen_[cursorRow_];
    const int amount = std::min(std::max(1, count), columns_ - cursorColumn_);
    for (int column = cursorColumn_; column < columns_ - amount; ++column) {
        line[column] = line[column + amount];
    }
    for (int column = columns_ - amount; column < columns_; ++column) {
        line[column] = TerminalCell();
    }
    normalizeWideCells(line);
}

void TerminalBuffer::insertLines(int count)
{
    pendingWrap_ = false;
    if (cursorRow_ < scrollTop_ || cursorRow_ > scrollBottom_) {
        return;
    }

    const int amount = std::min(std::max(1, count), scrollBottom_ - cursorRow_ + 1);
    for (int i = 0; i < amount; ++i) {
        screen_.insert(cursorRow_, blankLine());
        screen_.removeAt(scrollBottom_ + 1);
    }
}

void TerminalBuffer::deleteLines(int count)
{
    pendingWrap_ = false;
    if (cursorRow_ < scrollTop_ || cursorRow_ > scrollBottom_) {
        return;
    }

    const int amount = std::min(std::max(1, count), scrollBottom_ - cursorRow_ + 1);
    for (int i = 0; i < amount; ++i) {
        screen_.removeAt(cursorRow_);
        screen_.insert(scrollBottom_, blankLine());
    }
}

void TerminalBuffer::scrollUpLines(int count)
{
    const int amount = std::min(std::max(1, count), scrollBottom_ - scrollTop_ + 1);
    for (int i = 0; i < amount; ++i) {
        if (scrollTop_ == 0 && scrollBottom_ == rows_ - 1) {
            history_.append(screen_.takeFirst());
            screen_.append(blankLine());
            trimHistory();
        } else {
            screen_.removeAt(scrollTop_);
            screen_.insert(scrollBottom_, blankLine());
        }
    }
}

void TerminalBuffer::scrollDownLines(int count)
{
    const int amount = std::min(std::max(1, count), scrollBottom_ - scrollTop_ + 1);
    for (int i = 0; i < amount; ++i) {
        screen_.removeAt(scrollBottom_);
        screen_.insert(scrollTop_, blankLine());
    }
}

void TerminalBuffer::setScrollRegion(int top, int bottom)
{
    pendingWrap_ = false;
    scrollTop_ = std::max(0, std::min(top, rows_ - 1));
    scrollBottom_ = std::max(scrollTop_, std::min(bottom, rows_ - 1));
    cursorRow_ = originMode_ ? scrollTop_ : 0;
    cursorColumn_ = 0;
    clampCursor();
}

void TerminalBuffer::resetScrollRegion()
{
    pendingWrap_ = false;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
}

void TerminalBuffer::setOriginMode(bool enabled)
{
    pendingWrap_ = false;
    originMode_ = enabled;
    moveCursorTo(0, 0);
}

bool TerminalBuffer::originMode() const
{
    return originMode_;
}

void TerminalBuffer::setAutoWrapEnabled(bool enabled)
{
    autoWrapEnabled_ = enabled;
    if (!autoWrapEnabled_) {
        pendingWrap_ = false;
    }
}

bool TerminalBuffer::autoWrapEnabled() const
{
    return autoWrapEnabled_;
}

int TerminalBuffer::columns() const
{
    return columns_;
}

int TerminalBuffer::rows() const
{
    return rows_;
}

int TerminalBuffer::cursorRow() const
{
    return cursorRow_;
}

int TerminalBuffer::cursorColumn() const
{
    return std::min(cursorColumn_, columns_ - 1);
}

int TerminalBuffer::cursorColumnRaw() const
{
    return cursorColumn_;
}

int TerminalBuffer::historyLineCount() const
{
    return history_.size();
}

int TerminalBuffer::totalLineCount() const
{
    return history_.size() + screen_.size();
}

const TerminalBuffer::Line &TerminalBuffer::lineAt(int logicalRow) const
{
    if (logicalRow < history_.size()) {
        return history_[logicalRow];
    }
    return screen_[logicalRow - history_.size()];
}

void TerminalBuffer::setBracketedPasteEnabled(bool enabled)
{
    bracketedPasteEnabled_ = enabled;
}

bool TerminalBuffer::bracketedPasteEnabled() const
{
    return bracketedPasteEnabled_;
}

void TerminalBuffer::setMouseTrackingMode(int mode)
{
    mouseTrackingMode_ = mode;
}

int TerminalBuffer::mouseTrackingMode() const
{
    return mouseTrackingMode_;
}

void TerminalBuffer::setSgrMouseEnabled(bool enabled)
{
    sgrMouseEnabled_ = enabled;
}

bool TerminalBuffer::sgrMouseEnabled() const
{
    return sgrMouseEnabled_;
}

void TerminalBuffer::setFocusEventReportingEnabled(bool enabled)
{
    focusEventReportingEnabled_ = enabled;
}

bool TerminalBuffer::focusEventReportingEnabled() const
{
    return focusEventReportingEnabled_;
}

void TerminalBuffer::setModifyOtherKeysMode(int mode)
{
    modifyOtherKeysMode_ = std::max(0, mode);
}

int TerminalBuffer::modifyOtherKeysMode() const
{
    return modifyOtherKeysMode_;
}

void TerminalBuffer::setKittyKeyboardFlags(int flags)
{
    kittyKeyboardFlags_ = std::max(0, flags);
}

int TerminalBuffer::kittyKeyboardFlags() const
{
    return kittyKeyboardFlags_;
}

bool TerminalBuffer::kittyKeyboardEnabled() const
{
    return kittyKeyboardFlags_ != 0;
}

void TerminalBuffer::setCellSize(int width, int height)
{
    cellWidth_ = std::max(1, width);
    cellHeight_ = std::max(1, height);
}

int TerminalBuffer::cellWidth() const
{
    return cellWidth_;
}

int TerminalBuffer::cellHeight() const
{
    return cellHeight_;
}

void TerminalBuffer::setCursorVisible(bool visible)
{
    cursorVisible_ = visible;
}

bool TerminalBuffer::cursorVisible() const
{
    return cursorVisible_;
}

void TerminalBuffer::setCursorBlink(bool blink)
{
    cursorBlink_ = blink;
}

bool TerminalBuffer::cursorBlink() const
{
    return cursorBlink_;
}

void TerminalBuffer::setCursorShape(CursorShape shape)
{
    cursorShape_ = shape;
}

TerminalBuffer::CursorShape TerminalBuffer::cursorShape() const
{
    return cursorShape_;
}

void TerminalBuffer::useAlternateScreen(bool enabled)
{
    if (alternateScreenActive_ == enabled) {
        return;
    }

    if (enabled) {
        mainHistory_ = history_;
        mainScreen_ = screen_;
        mainCursorRow_ = cursorRow_;
        mainCursorColumn_ = cursorColumn_;
        alternateScreen_ = QVector<Line>();
        alternateScreen_.reserve(rows_);
        for (int row = 0; row < rows_; ++row) {
            alternateScreen_.append(blankLine());
        }
        screen_ = alternateScreen_;
        history_.clear();
        pendingWrap_ = false;
        cursorRow_ = originMode_ ? scrollTop_ : 0;
        cursorColumn_ = 0;
        scrollTop_ = 0;
        scrollBottom_ = rows_ - 1;
    } else {
        alternateScreen_ = screen_;
        history_ = mainHistory_;
        screen_ = mainScreen_.isEmpty() ? screen_ : mainScreen_;
        pendingWrap_ = false;
        cursorRow_ = mainCursorRow_;
        cursorColumn_ = mainCursorColumn_;
        scrollTop_ = 0;
        scrollBottom_ = rows_ - 1;
    }

    alternateScreenActive_ = enabled;
    clampCursor();
}

bool TerminalBuffer::alternateScreenActive() const
{
    return alternateScreenActive_;
}

TerminalBuffer::Line TerminalBuffer::blankLine() const
{
    Line line;
    line.resize(columns_);
    for (int column = 0; column < columns_; ++column) {
        line[column] = TerminalCell();
    }
    return line;
}

int TerminalBuffer::characterWidth(QChar ch) const
{
    const uint u = ch.unicode();
    if (u == 0x00ad
        || (u >= 0x0300 && u <= 0x036f)
        || (u >= 0x0483 && u <= 0x0489)
        || (u >= 0x0591 && u <= 0x05bd)
        || u == 0x05bf
        || (u >= 0x05c1 && u <= 0x05c2)
        || (u >= 0x05c4 && u <= 0x05c5)
        || u == 0x05c7
        || (u >= 0x0610 && u <= 0x061a)
        || (u >= 0x064b && u <= 0x065f)
        || u == 0x0670
        || (u >= 0x06d6 && u <= 0x06dc)
        || (u >= 0x06df && u <= 0x06e4)
        || (u >= 0x06e7 && u <= 0x06e8)
        || (u >= 0x06ea && u <= 0x06ed)
        || u == 0x0711
        || (u >= 0x0730 && u <= 0x074a)
        || (u >= 0x07a6 && u <= 0x07b0)
        || (u >= 0x07eb && u <= 0x07f3)
        || (u >= 0x0816 && u <= 0x0819)
        || (u >= 0x081b && u <= 0x0823)
        || (u >= 0x0825 && u <= 0x0827)
        || (u >= 0x0829 && u <= 0x082d)
        || (u >= 0x0859 && u <= 0x085b)
        || (u >= 0x08d3 && u <= 0x08ff)
        || (u >= 0x0900 && u <= 0x0903)
        || (u >= 0x093a && u <= 0x093c)
        || (u >= 0x0941 && u <= 0x0948)
        || u == 0x094d
        || (u >= 0x0951 && u <= 0x0957)
        || (u >= 0x0962 && u <= 0x0963)
        || u == 0x0981
        || u == 0x09bc
        || (u >= 0x09c1 && u <= 0x09c4)
        || u == 0x09cd
        || (u >= 0x09e2 && u <= 0x09e3)
        || (u >= 0x0a01 && u <= 0x0a02)
        || u == 0x0a3c
        || (u >= 0x0a41 && u <= 0x0a42)
        || (u >= 0x0a47 && u <= 0x0a48)
        || (u >= 0x0a4b && u <= 0x0a4d)
        || u == 0x0a51
        || (u >= 0x0a70 && u <= 0x0a71)
        || u == 0x0a75
        || (u >= 0x0a81 && u <= 0x0a82)
        || u == 0x0abc
        || (u >= 0x0ac1 && u <= 0x0ac5)
        || (u >= 0x0ac7 && u <= 0x0ac8)
        || u == 0x0acd
        || (u >= 0x0ae2 && u <= 0x0ae3)
        || u == 0x0b01
        || u == 0x0b3c
        || u == 0x0b3f
        || (u >= 0x0b41 && u <= 0x0b44)
        || u == 0x0b4d
        || (u >= 0x0b56 && u <= 0x0b57)
        || (u >= 0x0b62 && u <= 0x0b63)
        || u == 0x0b82
        || u == 0x0bc0
        || u == 0x0bcd
        || u == 0x0c00
        || (u >= 0x0c3e && u <= 0x0c40)
        || (u >= 0x0c46 && u <= 0x0c48)
        || (u >= 0x0c4a && u <= 0x0c4d)
        || (u >= 0x0c55 && u <= 0x0c56)
        || (u >= 0x0c62 && u <= 0x0c63)
        || u == 0x0c81
        || u == 0x0cbc
        || u == 0x0cbf
        || u == 0x0cc6
        || (u >= 0x0ccc && u <= 0x0ccd)
        || (u >= 0x0ce2 && u <= 0x0ce3)
        || (u >= 0x0d00 && u <= 0x0d01)
        || (u >= 0x0d41 && u <= 0x0d44)
        || u == 0x0d4d
        || (u >= 0x0d62 && u <= 0x0d63)
        || u == 0x0dca
        || (u >= 0x0dd2 && u <= 0x0dd4)
        || u == 0x0dd6
        || u == 0x0e31
        || (u >= 0x0e34 && u <= 0x0e3a)
        || (u >= 0x0e47 && u <= 0x0e4e)
        || u == 0x0eb1
        || (u >= 0x0eb4 && u <= 0x0eb9)
        || (u >= 0x0ebb && u <= 0x0ebc)
        || (u >= 0x0ec8 && u <= 0x0ecd)
        || (u >= 0x0f18 && u <= 0x0f19)
        || u == 0x0f35
        || u == 0x0f37
        || u == 0x0f39
        || (u >= 0x0f71 && u <= 0x0f7e)
        || (u >= 0x0f80 && u <= 0x0f84)
        || (u >= 0x0f86 && u <= 0x0f87)
        || (u >= 0x0f8d && u <= 0x0f97)
        || (u >= 0x0f99 && u <= 0x0fbc)
        || u == 0x0fc6
        || (u >= 0x102d && u <= 0x1030)
        || (u >= 0x1032 && u <= 0x1037)
        || (u >= 0x1039 && u <= 0x103a)
        || (u >= 0x103d && u <= 0x103e)
        || (u >= 0x1058 && u <= 0x1059)
        || (u >= 0x105e && u <= 0x1060)
        || (u >= 0x1071 && u <= 0x1074)
        || u == 0x1082
        || (u >= 0x1085 && u <= 0x1086)
        || u == 0x108d
        || u == 0x109d
        || (u >= 0x135d && u <= 0x135f)
        || (u >= 0x1712 && u <= 0x1714)
        || (u >= 0x1732 && u <= 0x1734)
        || (u >= 0x1752 && u <= 0x1753)
        || (u >= 0x1772 && u <= 0x1773)
        || (u >= 0x17b4 && u <= 0x17b5)
        || (u >= 0x17b7 && u <= 0x17bd)
        || u == 0x17c6
        || (u >= 0x17c9 && u <= 0x17d3)
        || u == 0x17dd
        || (u >= 0x180b && u <= 0x180f)
        || (u >= 0x1885 && u <= 0x1886)
        || u == 0x18a9
        || (u >= 0x1920 && u <= 0x1922)
        || (u >= 0x1927 && u <= 0x1928)
        || u == 0x1932
        || (u >= 0x1939 && u <= 0x193b)
        || (u >= 0x1a17 && u <= 0x1a18)
        || u == 0x1a1b
        || u == 0x1a56
        || (u >= 0x1a58 && u <= 0x1a5e)
        || u == 0x1a60
        || u == 0x1a62
        || (u >= 0x1a65 && u <= 0x1a6c)
        || (u >= 0x1a73 && u <= 0x1a7c)
        || u == 0x1a7f
        || (u >= 0x1ab0 && u <= 0x1ace)
        || (u >= 0x1b00 && u <= 0x1b03)
        || u == 0x1b34
        || (u >= 0x1b36 && u <= 0x1b3a)
        || u == 0x1b3c
        || u == 0x1b42
        || (u >= 0x1b6b && u <= 0x1b73)
        || (u >= 0x1b80 && u <= 0x1b81)
        || (u >= 0x1ba2 && u <= 0x1ba5)
        || (u >= 0x1ba8 && u <= 0x1ba9)
        || (u >= 0x1bab && u <= 0x1bad)
        || u == 0x1be6
        || (u >= 0x1be8 && u <= 0x1be9)
        || u == 0x1bed
        || (u >= 0x1bef && u <= 0x1bf1)
        || (u >= 0x1c2c && u <= 0x1c33)
        || (u >= 0x1c36 && u <= 0x1c37)
        || (u >= 0x1cd0 && u <= 0x1cd2)
        || (u >= 0x1cd4 && u <= 0x1ce0)
        || (u >= 0x1ce2 && u <= 0x1ce8)
        || u == 0x1ced
        || u == 0x1cf4
        || (u >= 0x1cf8 && u <= 0x1cf9)
        || (u >= 0x1dc0 && u <= 0x1dff)
        || (u >= 0x200b && u <= 0x200f)
        || (u >= 0x202a && u <= 0x202e)
        || (u >= 0x2060 && u <= 0x206f)
        || (u >= 0x20d0 && u <= 0x20ff)
        || (u >= 0xfe00 && u <= 0xfe0f)
        || (u >= 0xfe20 && u <= 0xfe2f)) {
        return 0;
    }

    if ((u >= 0x1100 && u <= 0x115f)
        || u == 0x2329
        || u == 0x232a
        || (u >= 0x2e80 && u <= 0xa4cf)
        || (u >= 0xac00 && u <= 0xd7a3)
        || (u >= 0xf900 && u <= 0xfaff)
        || (u >= 0xfe10 && u <= 0xfe19)
        || (u >= 0xfe30 && u <= 0xfe6f)
        || (u >= 0xff00 && u <= 0xff60)
        || (u >= 0xffe0 && u <= 0xffe6)
        || (u >= 0xd800 && u <= 0xdfff)
        || (u >= 0x1f300 && u <= 0x1f64f)
        || (u >= 0x1f900 && u <= 0x1f9ff)) {
        return 2;
    }
    return 1;
}

int TerminalBuffer::characterWidth(const QString &text) const
{
    int width = 0;
    for (QChar ch : text) {
        width = std::max(width, characterWidth(ch));
    }
    return width;
}

void TerminalBuffer::normalizeWideCells(Line &line)
{
    for (int column = 0; column < line.size(); ++column) {
        TerminalCell &cell = line[column];
        if (cell.wideContinuation) {
            const bool valid = column > 0 && line[column - 1].wide && !line[column - 1].text.isEmpty();
            if (!valid) {
                cell = TerminalCell();
            }
            continue;
        }

        if (cell.wide) {
            const bool hasContinuation = column + 1 < line.size() && line[column + 1].wideContinuation;
            if (!hasContinuation) {
                cell = TerminalCell();
                continue;
            }
            if (cell.text.isEmpty()) {
                cell = TerminalCell();
                line[column + 1] = TerminalCell();
            }
            continue;
        }

        if (column + 1 < line.size() && line[column + 1].wideContinuation) {
            line[column + 1] = TerminalCell();
        }
    }
}

void TerminalBuffer::resizeLines(QVector<Line> &lines, int oldColumns)
{
    for (Line &line : lines) {
        line.resize(columns_);
        for (int column = oldColumns; column < columns_; ++column) {
            line[column] = TerminalCell();
        }
        for (int column = 0; column < columns_; ++column) {
            if (line[column].text.isEmpty() && !line[column].wideContinuation) {
                line[column] = TerminalCell();
            }
        }
        normalizeWideCells(line);
    }
}

void TerminalBuffer::scrollUp()
{
    scrollUpLines(1);
}

void TerminalBuffer::eraseCell(int row, int column)
{
    if (row < 0 || row >= screen_.size() || column < 0 || column >= screen_[row].size()) {
        return;
    }

    Line &line = screen_[row];
    if (line[column].wideContinuation && column > 0) {
        line[column - 1] = TerminalCell();
    }
    if (line[column].wide && column + 1 < line.size()) {
        line[column + 1] = TerminalCell();
    }
    line[column] = TerminalCell();
}

void TerminalBuffer::clampCursor()
{
    cursorRow_ = std::max(0, std::min(cursorRow_, rows_ - 1));
    cursorColumn_ = std::max(0, std::min(cursorColumn_, columns_ - 1));
}

void TerminalBuffer::trimHistory()
{
    while (history_.size() > maxHistoryLines_) {
        history_.removeFirst();
    }
}
