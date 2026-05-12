#pragma once

#include "TerminalBuffer.h"

#include <QObject>
#include <QString>
#include <QStringList>

class TerminalEmulator : public QObject
{
    Q_OBJECT

public:
    explicit TerminalEmulator(QObject *parent = nullptr);
    void reset(TerminalBuffer *buffer);
    void process(const QString &text);

signals:
    void responseRequested(const QString &text);
    void titleChanged(const QString &title);

private:
    void flushText();
    void handleEscape();
    void handleCsi(const QString &params, QChar finalChar);
    void handleOsc(const QString &text);
    void handlePrivateMode(const QString &params, bool enabled);
    void handleKeyModifierOptions(const QString &params);
    void queryKeyModifierOptions(const QString &params);
    void handleKittyKeyboardProtocol(const QString &params);
    void queryKittyKeyboardProtocol();
    void handleCursorStyle(int value);
    void sendDeviceStatusReport(int value);
    void sendWindowReport(int value);
    void handleSgr(const QStringList &parts);
    QChar mapCharacter(QChar ch) const;
    QColor colorFromAnsiIndex(int index) const;
    QColor colorFrom256Index(int index) const;

    TerminalBuffer *buffer_ = nullptr;
    QString pendingText_;
    QString pendingEscape_;
    TerminalCell currentAttributes_;
    bool inEscape_ = false;
    bool inCsi_ = false;
    bool inOsc_ = false;
    QString oscText_;
    bool lineDrawingCharset_ = false;
    QString csiParams_;
};
