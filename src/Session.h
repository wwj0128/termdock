#pragma once

#include "ConPtyProcess.h"
#include "TerminalBuffer.h"
#include "TerminalEmulator.h"

#include <QObject>
#include <QString>

#include <memory>

class Session : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Starting,
        Running,
        Exited,
        Error
    };

    Session(int id, const QString &name, QObject *parent = nullptr);
    ~Session() override;

    int id() const;
    QString name() const;
    QString terminalTitle() const;
    QString workingDirectory() const;
    DWORD processId() const;
    State state() const;
    TerminalBuffer *terminalBuffer();
    const TerminalBuffer *terminalBuffer() const;

    bool start(const QString &workingDirectory = QString());
    void sendText(const QString &text);
    void sendRawText(const QString &text);
    void resizeTerminal(int columns, int rows);
    void clearOutput();
    void stop();

signals:
    void terminalUpdated(int sessionId);
    void titleChanged(int sessionId);
    void stateChanged(int sessionId);
    void errorOccurred(int sessionId, const QString &message);

private:
    void appendOutput(const QString &text);
    void setTerminalTitle(const QString &title);

    int id_ = 0;
    QString name_;
    QString workingDirectory_;
    QString terminalTitle_;
    DWORD processId_ = 0;
    State state_ = State::Starting;
    TerminalBuffer terminalBuffer_;
    TerminalEmulator terminalEmulator_;
    std::unique_ptr<ConPtyProcess> process_;
};
