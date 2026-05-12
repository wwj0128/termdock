#pragma once

#include "TerminalCell.h"

#include <QVector>

class TerminalBuffer
{
public:
    using Line = QVector<TerminalCell>;
    enum class CursorShape {
        Block,
        Underline,
        Bar,
    };

    TerminalBuffer(int columns = 120, int rows = 30);

    void resize(int columns, int rows);
    void clearScreen();
    void clearAll();
    void clearLine(int mode);
    void clearScreenMode(int mode);

    void putChar(QChar ch, const TerminalCell &attributes = TerminalCell());
    void putText(const QString &text, const TerminalCell &attributes = TerminalCell());
    void carriageReturn();
    void lineFeed();
    void reverseIndex();
    void backspace();
    void tab();

    void moveCursorRelative(int rowDelta, int columnDelta);
    void moveCursorTo(int row, int column);
    void moveCursorColumn(int column);
    void moveCursorRow(int row);
    void moveCursorNextLine(int count);
    void moveCursorPreviousLine(int count);
    void saveCursor();
    void restoreCursor();

    void eraseCharacters(int count);
    void insertCharacters(int count);
    void deleteCharacters(int count);
    void insertLines(int count);
    void deleteLines(int count);
    void scrollUpLines(int count);
    void scrollDownLines(int count);
    void setScrollRegion(int top, int bottom);
    void resetScrollRegion();
    void setOriginMode(bool enabled);
    bool originMode() const;
    void setAutoWrapEnabled(bool enabled);
    bool autoWrapEnabled() const;

    int columns() const;
    int rows() const;
    int cursorRow() const;
    int cursorColumn() const;
    int cursorColumnRaw() const;
    int historyLineCount() const;
    int totalLineCount() const;
    const Line &lineAt(int logicalRow) const;
    void setBracketedPasteEnabled(bool enabled);
    bool bracketedPasteEnabled() const;
    void setMouseTrackingMode(int mode);
    int mouseTrackingMode() const;
    void setSgrMouseEnabled(bool enabled);
    bool sgrMouseEnabled() const;
    void setFocusEventReportingEnabled(bool enabled);
    bool focusEventReportingEnabled() const;
    void setModifyOtherKeysMode(int mode);
    int modifyOtherKeysMode() const;
    void setKittyKeyboardFlags(int flags);
    int kittyKeyboardFlags() const;
    bool kittyKeyboardEnabled() const;
    void setCellSize(int width, int height);
    int cellWidth() const;
    int cellHeight() const;
    void setCursorVisible(bool visible);
    bool cursorVisible() const;
    void setCursorBlink(bool blink);
    bool cursorBlink() const;
    void setCursorShape(CursorShape shape);
    CursorShape cursorShape() const;
    void useAlternateScreen(bool enabled);
    bool alternateScreenActive() const;

private:
    Line blankLine() const;
    int characterWidth(QChar ch) const;
    int characterWidth(const QString &text) const;
    void normalizeWideCells(Line &line);
    void resizeLines(QVector<Line> &lines, int oldColumns);
    void eraseCell(int row, int column);
    void scrollUp();
    void clampCursor();
    void trimHistory();

    int columns_ = 120;
    int rows_ = 30;
    int cursorRow_ = 0;
    int cursorColumn_ = 0;
    int savedCursorRow_ = 0;
    int savedCursorColumn_ = 0;
    int scrollTop_ = 0;
    int scrollBottom_ = 29;
    int maxHistoryLines_ = 10000;
    bool bracketedPasteEnabled_ = false;
    int mouseTrackingMode_ = 0;
    bool sgrMouseEnabled_ = false;
    bool focusEventReportingEnabled_ = false;
    int modifyOtherKeysMode_ = 0;
    int kittyKeyboardFlags_ = 0;
    int cellWidth_ = 8;
    int cellHeight_ = 16;
    bool cursorVisible_ = true;
    bool cursorBlink_ = true;
    bool originMode_ = false;
    bool autoWrapEnabled_ = true;
    bool pendingWrap_ = false;
    bool alternateScreenActive_ = false;
    CursorShape cursorShape_ = CursorShape::Block;
    QVector<Line> history_;
    QVector<Line> screen_;
    QVector<Line> mainHistory_;
    QVector<Line> mainScreen_;
    QVector<Line> alternateScreen_;
    int mainCursorRow_ = 0;
    int mainCursorColumn_ = 0;
};
