#include "Session.h"

Session::Session(int id, const QString &name, QObject *parent)
    : QObject(parent)
    , id_(id)
    , name_(name)
{
    terminalEmulator_.reset(&terminalBuffer_);
    connect(&terminalEmulator_, &TerminalEmulator::responseRequested, this, [this](const QString &text) {
        if (state_ == State::Running && process_) {
            process_->writeRawInput(text.toUtf8());
        }
    });
    connect(&terminalEmulator_, &TerminalEmulator::titleChanged, this, &Session::setTerminalTitle);
}

Session::~Session()
{
    stop();
}

int Session::id() const
{
    return id_;
}

QString Session::name() const
{
    return name_;
}

QString Session::terminalTitle() const
{
    return terminalTitle_;
}

QString Session::workingDirectory() const
{
    return workingDirectory_;
}

DWORD Session::processId() const
{
    return processId_;
}

Session::State Session::state() const
{
    return state_;
}

TerminalBuffer *Session::terminalBuffer()
{
    return &terminalBuffer_;
}

const TerminalBuffer *Session::terminalBuffer() const
{
    return &terminalBuffer_;
}

bool Session::start(const QString &workingDirectory)
{
    workingDirectory_ = workingDirectory;
    process_ = std::make_unique<ConPtyProcess>();

    connect(process_.get(), &ConPtyProcess::outputReceived, this, [this](const QString &text) {
        appendOutput(text);
    });

    connect(process_.get(), &ConPtyProcess::exited, this, [this](int exitCode) {
        state_ = State::Exited;
        appendOutput(QStringLiteral("\r\n[process exited with code %1]\r\n").arg(exitCode));
        emit stateChanged(id_);
    });

    connect(process_.get(), &ConPtyProcess::errorOccurred, this, [this](const QString &message) {
        state_ = State::Error;
        appendOutput(QStringLiteral("\r\n[error] %1\r\n").arg(message));
        emit errorOccurred(id_, message);
        emit stateChanged(id_);
    });

    if (!process_->start(workingDirectory, terminalBuffer_.columns(), terminalBuffer_.rows())) {
        state_ = State::Error;
        emit stateChanged(id_);
        return false;
    }

    processId_ = process_->processId();
    state_ = State::Running;
    emit stateChanged(id_);
    return true;
}

void Session::sendText(const QString &text)
{
    if (state_ != State::Running || !process_) {
        appendOutput(QStringLiteral("\r\n[session is not running]\r\n"));
        return;
    }

    process_->writeInput(text);
}

void Session::sendRawText(const QString &text)
{
    if (state_ != State::Running || !process_) {
        return;
    }

    process_->writeRawInput(text.toUtf8());
}

void Session::resizeTerminal(int columns, int rows)
{
    terminalBuffer_.resize(columns, rows);
    if (process_) {
        process_->resize(columns, rows);
    }
    emit terminalUpdated(id_);
}

void Session::clearOutput()
{
    terminalBuffer_.clearAll();
    terminalEmulator_.reset(&terminalBuffer_);
    emit terminalUpdated(id_);
}

void Session::stop()
{
    if (process_) {
        process_->close();
        process_.reset();
    }

    if (state_ == State::Running || state_ == State::Starting) {
        state_ = State::Exited;
        emit stateChanged(id_);
    }
}

void Session::appendOutput(const QString &text)
{
    terminalEmulator_.process(text);
    emit terminalUpdated(id_);
}

void Session::setTerminalTitle(const QString &title)
{
    if (terminalTitle_ == title) {
        return;
    }

    terminalTitle_ = title;
    emit titleChanged(id_);
}
