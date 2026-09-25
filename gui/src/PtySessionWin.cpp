// ConPTY-backed implementation of PtySession, for a native Windows build.
// See PtySession.h for the interface contract and PtySessionUnix.cpp for
// the POSIX/forkpty sibling this mirrors.
//
// Validated on Windows (phase 4): builds clean with MSVC, and attach,
// keyboard input, SGR color, alt-screen, and live resize have all been
// confirmed against a real Docker Desktop (WSL2) container. The one
// non-obvious fix required: STARTF_USESTDHANDLES must be set with null
// handles so ConPTY can wire all three stdio streams to the pseudoconsole
// -- without it, CreateProcessW inherits this (windowless) process's own
// std-handle values, GetConsoleMode fails on them in the child, and
// `docker attach` quits immediately with "cannot attach stdin to a
// TTY-enabled container because stdin is not a terminal". See the comment
// on startupInfo.StartupInfo.dwFlags below.

#include "PtySession.h"

#include <QThread>
#include <QWinEventNotifier>

#include <string>
#include <vector>

namespace {

// CreateProcessW takes one command-line string, not an argv array, and
// Windows' own parsing rules (shared by CommandLineToArgvW, and therefore
// by anything that reads argv the normal way) are: a "\"" is escaped as
// "\\\"", and backslashes are only special immediately before a quote --
// N backslashes before a literal quote become 2N+1 backslashes plus the
// escaped quote, while backslashes anywhere else (including at the very
// end of the whole argument) pass through unchanged. Getting this wrong
// is a classic source of Windows subprocess bugs, so it gets its own
// well-commented function rather than an inline join.
QString quoteWindowsArg(const QString &arg)
{
    if (!arg.isEmpty() && arg.indexOf(QLatin1Char(' ')) < 0 && arg.indexOf(QLatin1Char('\t')) < 0
        && arg.indexOf(QLatin1Char('"')) < 0) {
        return arg; // nothing that needs quoting or escaping
    }

    QString out = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar &c : arg) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"')) {
            out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            out += QLatin1Char('"');
            backslashes = 0;
            continue;
        }
        out += QString(backslashes, QLatin1Char('\\'));
        backslashes = 0;
        out += c;
    }
    // Backslashes right before the closing quote must be doubled, or
    // they'd escape that quote instead of terminating the argument.
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    return out;
}

QString buildCommandLine(const QString &program, const QStringList &args)
{
    QStringList parts;
    parts << quoteWindowsArg(program);
    for (const QString &a : args)
        parts << quoteWindowsArg(a);
    return parts.join(QLatin1Char(' '));
}

} // namespace

// Blocks in ReadFile for as long as the pipe lives; has no event loop of
// its own and needs none; run() is entered once, via QThread::started(),
// and doesn't return until the pipe breaks (child exited).
class PtyReader : public QObject {
    Q_OBJECT
public:
    explicit PtyReader(HANDLE outputRead) : m_outputRead(outputRead) {}

public slots:
    void run()
    {
        char buffer[4096];
        DWORD bytesRead = 0;
        while (::ReadFile(m_outputRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            emit dataReady(QByteArray(buffer, int(bytesRead)));
        // ReadFile returning false (or, defensively, 0 bytes with no
        // error) means the pipe is gone -- the child exited and ConPTY
        // tore its end down. Exit itself is reported separately, via
        // QWinEventNotifier on the process handle in PtySession, not from
        // here; this thread's only job was the bytes.
    }

signals:
    void dataReady(const QByteArray &data);

private:
    HANDLE m_outputRead;
};

// The write side gets its own thread specifically so it keeps a normal Qt
// event loop: unlike PtyReader, this class's slot is invoked the ordinary
// queued way (QThread's default run() calls exec(), which is exactly what
// dispatches queued slot calls) -- sharing PtyReader's thread would leave
// queued writes stuck behind its permanently-blocked ReadFile loop.
class PtyWriter : public QObject {
    Q_OBJECT
public:
    explicit PtyWriter(HANDLE inputWrite) : m_inputWrite(inputWrite) {}

public slots:
    void writeData(const QByteArray &data)
    {
        const char *p = data.constData();
        int remaining = data.size();
        while (remaining > 0) {
            DWORD written = 0;
            if (!::WriteFile(m_inputWrite, p, DWORD(remaining), &written, nullptr))
                return; // pipe gone; matches the POSIX side's silent drop on error
            p += written;
            remaining -= int(written);
        }
    }

private:
    HANDLE m_inputWrite;
};

PtySession::PtySession(QObject *parent) : QObject(parent) {}

PtySession::~PtySession()
{
    cleanup();
}

bool PtySession::start(const QString &program, const QStringList &args, const QString &workingDir)
{
    cleanup(); // in case of a restart -- mirrors the POSIX side's guard

    HANDLE inputReadSide = nullptr, inputWriteSide = nullptr;
    HANDLE outputReadSide = nullptr, outputWriteSide = nullptr;
    if (!::CreatePipe(&inputReadSide, &inputWriteSide, nullptr, 0))
        return false;
    if (!::CreatePipe(&outputReadSide, &outputWriteSide, nullptr, 0)) {
        ::CloseHandle(inputReadSide);
        ::CloseHandle(inputWriteSide);
        return false;
    }

    // Initial size is corrected immediately by the first real resize()
    // from TerminalWidget::updateGridSize() -- matches the POSIX side's
    // hardcoded 24x80 starting winsize for the same reason.
    const HRESULT hr = ::CreatePseudoConsole({80, 24}, inputReadSide, outputWriteSide, 0, &m_pseudoConsole);

    // Per Microsoft's sample: once ConPTY has the console-facing ends, our
    // copies of them are just extra handles keeping the pipe alive longer
    // than it should be. Close them regardless of success so failure
    // doesn't leak two handles.
    ::CloseHandle(inputReadSide);
    ::CloseHandle(outputWriteSide);

    if (FAILED(hr)) {
        ::CloseHandle(inputWriteSide);
        ::CloseHandle(outputReadSide);
        m_pseudoConsole = nullptr;
        return false;
    }

    m_inputWrite = inputWriteSide;
    m_outputRead = outputReadSide;

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);

    // STARTF_USESTDHANDLES with all three handles null, even though ConPTY is
    // what actually supplies the child's stdio. Without it, CreateProcessW
    // copies *this* process's std-handle values into the child's process
    // parameters; in a GUI process with no console of its own those values are
    // meaningless in the child, and -- the non-obvious part -- the
    // PSEUDOCONSOLE attribute does not overwrite them when they're already
    // set. The child then sees stdin/stdout/stderr as non-console
    // (GetConsoleMode fails), so anything that probes for a tty misbehaves:
    // `docker attach` to a -t container quits with "cannot attach stdin to a
    // TTY-enabled container because stdin is not a terminal". Passing null
    // handles here leaves the field clear for ConPTY to wire all three to the
    // pseudoconsole. (Confirmed against a standalone ConPTY harness: without
    // this the child's handles are FILE_TYPE_DISK/UNKNOWN; with it they are
    // proper console handles.)
    startupInfo.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    startupInfo.StartupInfo.hStdInput = nullptr;
    startupInfo.StartupInfo.hStdOutput = nullptr;
    startupInfo.StartupInfo.hStdError = nullptr;

    SIZE_T attrListSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    std::vector<char> attrListBuffer(attrListSize);
    startupInfo.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrListBuffer.data());
    if (!::InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attrListSize)) {
        cleanup();
        return false;
    }
    if (!::UpdateProcThreadAttribute(startupInfo.lpAttributeList, 0,
                                     PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_pseudoConsole,
                                     sizeof(HPCON), nullptr, nullptr)) {
        ::DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
        cleanup();
        return false;
    }

    const std::wstring commandLineW = buildCommandLine(program, args).toStdWString();
    std::vector<wchar_t> commandLineBuffer(commandLineW.begin(), commandLineW.end());
    commandLineBuffer.push_back(L'\0'); // CreateProcessW may write into this buffer in place

    const std::wstring workingDirW = workingDir.toStdWString();

    PROCESS_INFORMATION processInfo{};
    const BOOL spawned = ::CreateProcessW(
        nullptr, commandLineBuffer.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT, nullptr,
        workingDir.isEmpty() ? nullptr : workingDirW.c_str(),
        &startupInfo.StartupInfo, &processInfo);

    ::DeleteProcThreadAttributeList(startupInfo.lpAttributeList);

    if (!spawned) {
        cleanup();
        return false;
    }

    ::CloseHandle(processInfo.hThread);
    m_processHandle = processInfo.hProcess;

    m_readThread = new QThread(this);
    m_reader = new PtyReader(m_outputRead); // no parent -- moveToThread requires that
    m_reader->moveToThread(m_readThread);
    connect(m_readThread, &QThread::started, m_reader, &PtyReader::run);
    connect(m_reader, &PtyReader::dataReady, this, &PtySession::dataReady);
    m_readThread->start();

    m_writeThread = new QThread(this);
    m_writer = new PtyWriter(m_inputWrite); // no parent -- same reason
    m_writer->moveToThread(m_writeThread);
    m_writeThread->start();

    m_exitNotifier = new QWinEventNotifier(m_processHandle, this);
    connect(m_exitNotifier, &QWinEventNotifier::activated, this, &PtySession::onProcessExited);

    return true;
}

bool PtySession::isRunning() const
{
    if (!m_processHandle)
        return false;
    return ::WaitForSingleObject(m_processHandle, 0) == WAIT_TIMEOUT;
}

void PtySession::resize(int rows, int cols)
{
    if (!m_pseudoConsole)
        return;
    ::ResizePseudoConsole(m_pseudoConsole, {SHORT(cols), SHORT(rows)});
}

void PtySession::write(const QByteArray &data)
{
    if (!m_writer)
        return;
    // Queued onto the writer thread rather than called directly: write()
    // runs on the GUI thread (TerminalWidget calls it straight from a key
    // event / libvterm's output callback), and WriteFile blocking there
    // even briefly is exactly what PtyWriter's own thread exists to avoid.
    QMetaObject::invokeMethod(m_writer, "writeData", Qt::QueuedConnection, Q_ARG(QByteArray, data));
}

void PtySession::onProcessExited()
{
    DWORD exitCode = DWORD(-1);
    ::GetExitCodeProcess(m_processHandle, &exitCode);
    cleanup();
    emit finished(int(exitCode));
}

void PtySession::cleanup()
{
    // Order matters: close the pipe handles before tearing down the
    // threads that block on them -- a ReadFile pending on a handle that
    // gets closed from another thread returns with an error immediately,
    // which is precisely how PtyReader's loop is meant to end. Tearing
    // the thread down first would just hang waiting for a read that will
    // never complete.
    if (m_exitNotifier) {
        m_exitNotifier->setEnabled(false);
        m_exitNotifier->deleteLater();
        m_exitNotifier = nullptr;
    }

    if (m_inputWrite) {
        ::CloseHandle(m_inputWrite);
        m_inputWrite = nullptr;
    }
    if (m_outputRead) {
        ::CloseHandle(m_outputRead);
        m_outputRead = nullptr;
    }

    // wait() only returns once the underlying OS thread has actually
    // stopped running, so a plain synchronous delete of the worker right
    // after it -- rather than queuing a deleteLater() onto the worker's
    // own thread -- is both safe (nothing can still be touching it) and
    // necessary: PtyReader's thread never runs an event loop at all
    // (run() blocks instead of returning to one), and PtyWriter's has
    // already been told to quit, so a deleteLater() queued onto either
    // would sit in a queue nothing will ever drain again.
    if (m_readThread) {
        m_readThread->quit();
        m_readThread->wait();
        delete m_reader;
        m_reader = nullptr;
        m_readThread->deleteLater(); // the QThread object itself lives on
        m_readThread = nullptr;      // the GUI thread, so this one is fine
    }
    if (m_writeThread) {
        m_writeThread->quit();
        m_writeThread->wait();
        delete m_writer;
        m_writer = nullptr;
        m_writeThread->deleteLater();
        m_writeThread = nullptr;
    }

    if (m_pseudoConsole) {
        ::ClosePseudoConsole(m_pseudoConsole);
        m_pseudoConsole = nullptr;
    }
    if (m_processHandle) {
        // Mirrors the POSIX destructor's kill()+waitpid(): make sure the
        // child is actually gone rather than leaving it to float free.
        ::TerminateProcess(m_processHandle, 1);
        ::CloseHandle(m_processHandle);
        m_processHandle = nullptr;
    }
}

#include "PtySessionWin.moc"
