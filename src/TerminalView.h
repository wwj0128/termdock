#pragma once

#include "TerminalBuffer.h"

#include <QAbstractScrollArea>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QVariant>

class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class TerminalView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TerminalView(QWidget *parent = nullptr);

    void setBuffer(TerminalBuffer *buffer);
    void refresh();
    void syncSizeToPty();
    int columnCount() const;
    int rowCount() const;
    bool copySelection();
    bool pasteFromClipboard();

signals:
    void rawInputRequested(const QString &text);
    void terminalSizeChanged(int columns, int rows);
    void interactionMessage(const QString &message);

protected:
    bool event(QEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateMetrics();
    bool syncTerminalSize();
    void updateScrollBar(bool stickToBottom);
    bool isAtBottom() const;
    int visibleRowCount() const;
    QString terminalKeySequence(QKeyEvent *event) const;
    QString modifiedPrintableKeySequence(QKeyEvent *event) const;
    QString extendedKeySequence(int codepoint, const QString &modifier) const;
    QString modifierParameter(Qt::KeyboardModifiers modifiers) const;
    QPoint cellAtPosition(const QPoint &position) const;
    int normalizedSelectionStart() const;
    int normalizedSelectionEnd() const;
    bool hasSelection() const;
    bool isCellSelected(int logicalRow, int column) const;
    QString selectedText() const;
    void copySelectionToClipboard();
    void pasteClipboard();
    void clearSelection();
    bool sendMouseEvent(QMouseEvent *event, bool release, bool drag = false);
    bool sendWheelEvent(QWheelEvent *event);
    QString mouseReport(int button, int column, int row, bool release) const;
    QRect inputMethodCursorRectangle() const;
    void updateInputMethodCursor();
    QString inputWithCommandAliases(const QString &input);

    TerminalBuffer *buffer_ = nullptr;
    QTimer cursorTimer_;
    bool cursorVisible_ = true;
    bool selecting_ = false;
    QPoint selectionAnchor_;
    QPoint selectionCurrent_;
    QString preeditText_;
    QString currentInputLine_;
    int preeditCursor_ = 0;
    int pressedMouseButton_ = -1;
    int syncedColumns_ = 0;
    int syncedRows_ = 0;
    int cellWidth_ = 8;
    int cellHeight_ = 16;
    int baseline_ = 12;
};
