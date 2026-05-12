#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#else
#include <QTextCodec>
#endif

#include <atomic>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class ConPtyProcess : public QObject
{
    Q_OBJECT

public:
    explicit ConPtyProcess(QObject *parent = nullptr);
    ~ConPtyProcess() override;

    bool start(const QString &workingDirectory = QString(), int columns = 120, int rows = 30);
    bool writeInput(const QString &text);
    bool writeRawInput(const QByteArray &bytes);
    void resize(int columns, int rows);
    void close();

    DWORD processId() const;
    bool isRunning() const;

signals:
    void outputReceived(const QString &text);
    void exited(int exitCode);
    void errorOccurred(const QString &message);

private:
    using PseudoConsoleHandle = void *;
    using CreatePseudoConsoleFn = HRESULT (WINAPI *)(COORD, HANDLE, HANDLE, DWORD, PseudoConsoleHandle *);
    using ResizePseudoConsoleFn = HRESULT (WINAPI *)(PseudoConsoleHandle, COORD);
    using ClosePseudoConsoleFn = void (WINAPI *)(PseudoConsoleHandle);

    bool loadApi();
    bool createPipes();
    bool createPseudoConsole(int columns, int rows);
    bool launchProcess(const QString &workingDirectory);
    void startReader();
    void readerLoop();
    void cleanupAttributeList(PPROC_THREAD_ATTRIBUTE_LIST attributeList);
    void closeHandle(HANDLE &handle);
    void emitLastError(const QString &context);
    QString lastErrorMessage() const;

    HMODULE kernel32_ = nullptr;
    CreatePseudoConsoleFn createPseudoConsole_ = nullptr;
    ResizePseudoConsoleFn resizePseudoConsole_ = nullptr;
    ClosePseudoConsoleFn closePseudoConsole_ = nullptr;

    HANDLE inputRead_ = nullptr;
    HANDLE inputWrite_ = nullptr;
    HANDLE outputRead_ = nullptr;
    HANDLE outputWrite_ = nullptr;
    HANDLE processHandle_ = nullptr;
    HANDLE threadHandle_ = nullptr;
    PseudoConsoleHandle pseudoConsole_ = nullptr;
    DWORD processId_ = 0;

    std::thread readerThread_;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QStringDecoder outputDecoder_{QStringDecoder::Utf8};
#else
    QTextCodec::ConverterState outputDecoderState_;
#endif
    std::atomic_bool running_{false};
    std::atomic_bool closing_{false};
};
