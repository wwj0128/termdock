#include "ConPtyProcess.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <vector>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

ConPtyProcess::ConPtyProcess(QObject *parent)
    : QObject(parent)
{
}

ConPtyProcess::~ConPtyProcess()
{
    close();
}

bool ConPtyProcess::start(const QString &workingDirectory, int columns, int rows)
{
    if (running_) {
        return true;
    }

    closing_ = false;

    if (!loadApi() || !createPipes() || !createPseudoConsole(columns, rows) || !launchProcess(workingDirectory)) {
        close();
        return false;
    }

    closeHandle(inputRead_);
    closeHandle(outputWrite_);

    running_ = true;
    startReader();
    return true;
}

bool ConPtyProcess::writeInput(const QString &text)
{
    QByteArray bytes = text.toUtf8();
    if (!bytes.endsWith("\r\n")) {
        bytes.append("\r\n");
    }
    return writeRawInput(bytes);
}

bool ConPtyProcess::writeRawInput(const QByteArray &bytes)
{
    if (!running_ || !inputWrite_) {
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(inputWrite_, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    if (!ok || written != static_cast<DWORD>(bytes.size())) {
        emitLastError(QStringLiteral("WriteFile"));
        return false;
    }

    return true;
}

void ConPtyProcess::resize(int columns, int rows)
{
    if (!pseudoConsole_ || !resizePseudoConsole_ || columns <= 0 || rows <= 0) {
        return;
    }

    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    resizePseudoConsole_(pseudoConsole_, size);
}

void ConPtyProcess::close()
{
    if (closing_.exchange(true)) {
        return;
    }

    if (running_ && inputWrite_) {
        QByteArray exitCommand("exit\r\n");
        DWORD written = 0;
        WriteFile(inputWrite_, exitCommand.constData(), static_cast<DWORD>(exitCommand.size()), &written, nullptr);
    }

    closeHandle(inputWrite_);
    closeHandle(inputRead_);
    closeHandle(outputWrite_);

    if (processHandle_) {
        const DWORD waitResult = WaitForSingleObject(processHandle_, 700);
        if (waitResult == WAIT_TIMEOUT) {
            TerminateProcess(processHandle_, 1);
            WaitForSingleObject(processHandle_, 1000);
        }
    }

    if (pseudoConsole_ && closePseudoConsole_) {
        closePseudoConsole_(pseudoConsole_);
        pseudoConsole_ = nullptr;
    }

    running_ = false;
    closeHandle(outputRead_);

    if (readerThread_.joinable()) {
        readerThread_.join();
    }

    closeHandle(threadHandle_);
    closeHandle(processHandle_);
    processId_ = 0;
    closing_ = false;
}

DWORD ConPtyProcess::processId() const
{
    return processId_;
}

bool ConPtyProcess::isRunning() const
{
    return running_;
}

bool ConPtyProcess::loadApi()
{
    kernel32_ = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32_) {
        emitLastError(QStringLiteral("GetModuleHandleW(kernel32.dll)"));
        return false;
    }

    createPseudoConsole_ = reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(kernel32_, "CreatePseudoConsole"));
    resizePseudoConsole_ = reinterpret_cast<ResizePseudoConsoleFn>(GetProcAddress(kernel32_, "ResizePseudoConsole"));
    closePseudoConsole_ = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel32_, "ClosePseudoConsole"));

    if (!createPseudoConsole_ || !resizePseudoConsole_ || !closePseudoConsole_) {
        emit errorOccurred(QStringLiteral("ConPTY is not available. Windows 10 1809 or newer is required."));
        return false;
    }

    return true;
}

bool ConPtyProcess::createPipes()
{
    if (!CreatePipe(&inputRead_, &inputWrite_, nullptr, 0)) {
        emitLastError(QStringLiteral("CreatePipe(input)"));
        return false;
    }

    if (!CreatePipe(&outputRead_, &outputWrite_, nullptr, 0)) {
        emitLastError(QStringLiteral("CreatePipe(output)"));
        return false;
    }

    return true;
}

bool ConPtyProcess::createPseudoConsole(int columns, int rows)
{
    const COORD size{static_cast<SHORT>(std::max(20, columns)), static_cast<SHORT>(std::max(5, rows))};
    const HRESULT hr = createPseudoConsole_(size, inputRead_, outputWrite_, 0, &pseudoConsole_);
    if (FAILED(hr)) {
        emit errorOccurred(QStringLiteral("CreatePseudoConsole failed: 0x%1").arg(static_cast<unsigned long>(hr), 8, 16, QLatin1Char('0')));
        return false;
    }

    return true;
}

bool ConPtyProcess::launchProcess(const QString &workingDirectory)
{
    SIZE_T attributeListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);

    std::vector<char> attributeStorage(attributeListSize);
    auto *attributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize)) {
        emitLastError(QStringLiteral("InitializeProcThreadAttributeList"));
        return false;
    }

    if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudoConsole_, sizeof(pseudoConsole_), nullptr, nullptr)) {
        emitLastError(QStringLiteral("UpdateProcThreadAttribute(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE)"));
        DeleteProcThreadAttributeList(attributeList);
        return false;
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);
    startupInfo.lpAttributeList = attributeList;

    PROCESS_INFORMATION processInfo{};
    SetEnvironmentVariableW(L"TERM", L"xterm-256color");
    SetEnvironmentVariableW(L"COLORTERM", L"truecolor");
    SetEnvironmentVariableW(L"CLICOLOR_FORCE", L"1");
    SetEnvironmentVariableW(L"FORCE_COLOR", L"3");
    QString commandLine = QStringLiteral("\"C:\\Windows\\System32\\cmd.exe\" /K chcp 65001 > nul && cls");
    std::wstring commandLineW = commandLine.toStdWString();

    std::wstring workingDirectoryW;
    LPCWSTR workingDirectoryPtr = nullptr;
    if (!workingDirectory.isEmpty()) {
        workingDirectoryW = QDir::toNativeSeparators(workingDirectory).toStdWString();
        workingDirectoryPtr = workingDirectoryW.c_str();
    }

    const BOOL ok = CreateProcessW(
        nullptr,
        commandLineW.data(),
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        workingDirectoryPtr,
        &startupInfo.StartupInfo,
        &processInfo
    );

    DeleteProcThreadAttributeList(attributeList);

    if (!ok) {
        emitLastError(QStringLiteral("CreateProcessW(C:\\Windows\\System32\\cmd.exe)"));
        return false;
    }

    processHandle_ = processInfo.hProcess;
    threadHandle_ = processInfo.hThread;
    processId_ = processInfo.dwProcessId;
    return true;
}

void ConPtyProcess::startReader()
{
    readerThread_ = std::thread(&ConPtyProcess::readerLoop, this);
}

void ConPtyProcess::readerLoop()
{
    std::array<char, 4096> buffer{};

    while (running_) {
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(outputRead_, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
        if (!ok || bytesRead == 0) {
            break;
        }

        const QByteArray chunk(buffer.data(), static_cast<int>(bytesRead));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QString output = outputDecoder_.decode(chunk);
#else
        QTextCodec *codec = QTextCodec::codecForName("UTF-8");
        const QString output = codec->toUnicode(chunk.constData(), chunk.size(), &outputDecoderState_);
#endif
        if (!output.isEmpty()) {
            emit outputReceived(output);
        }
    }

    int exitCode = 0;
    if (processHandle_) {
        WaitForSingleObject(processHandle_, 0);
        DWORD code = 0;
        if (GetExitCodeProcess(processHandle_, &code) && code != STILL_ACTIVE) {
            exitCode = static_cast<int>(code);
        }
    }

    running_ = false;
    if (!closing_) {
        emit exited(exitCode);
    }
}

void ConPtyProcess::closeHandle(HANDLE &handle)
{
    if (handle) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

void ConPtyProcess::emitLastError(const QString &context)
{
    emit errorOccurred(QStringLiteral("%1 failed: %2").arg(context, lastErrorMessage()));
}

QString ConPtyProcess::lastErrorMessage() const
{
    const DWORD error = GetLastError();
    LPWSTR buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr
    );

    QString message;
    if (size && buffer) {
        message = QString::fromWCharArray(buffer, static_cast<int>(size)).trimmed();
        LocalFree(buffer);
    } else {
        message = QStringLiteral("Win32 error %1").arg(error);
    }

    return message;
}
