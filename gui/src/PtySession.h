#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#ifdef Q_OS_WIN
// ConPTY (CreatePseudoConsole & friends) is declared behind a Windows-10
// version gate in the SDK headers; the default target version on a lot of
// toolchains predates it, which silently makes those symbols vanish
// instead of erroring cleanly. Force it up before windows.h is read, no
// matter what a project-wide define elsewhere sets.
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006 // NTDDI_WIN10_RS5 (1809), where ConPTY shipped
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 // _WIN32_WINNT_WIN10
#endif
#include <windows.h>
class QThread;
class QWinEventNotifier;
class PtyReader;
class PtyWriter;
#else
#include <sys/types.h>
class QSocketNotifier;
#endif

// Owns one PTY + child process (used to run `docker attach <name>`).
// Event-driven by design, unlike DockerBackend's synchronous admin calls:
// this is a long-lived connection whose output can arrive at any time.
//
// Closing/destroying a session (or the remote process exiting on its own)
// does not affect the underlying docker container -- `docker attach`
// detaching doesn't stop what it's attached to, only `docker stop` does.
//
// Two implementations share this one interface: PtySessionUnix.cpp
// (forkpty) and PtySessionWin.cpp (ConPTY) -- exactly one is compiled in,
// chosen by CMakeLists.txt. TerminalWidget, the only consumer, talks to
// PtySession purely through the public members below and never reaches
// past them, which is what keeps the two implementations swappable.
class PtySession : public QObject {
    Q_OBJECT
public:
    explicit PtySession(QObject *parent = nullptr);
    ~PtySession() override;

    // Spawns `program args...` with stdio wired to a fresh PTY, chdir'd to
    // workingDir first if non-empty. Returns false if the PTY/fork setup
    // itself failed (a bad `program` still "succeeds" here and shows up
    // as an immediate `finished` with a non-zero/undefined exit code).
    bool start(const QString &program, const QStringList &args, const QString &workingDir = QString());

    bool isRunning() const;

    void resize(int rows, int cols);

public slots:
    void write(const QByteArray &data);

signals:
    void dataReady(const QByteArray &data);
    void finished(int exitCode);

private slots:
#ifdef Q_OS_WIN
    void onProcessExited();
#else
    void onMasterReadable();
#endif

private:
    void cleanup();

#ifdef Q_OS_WIN
    HPCON m_pseudoConsole = nullptr;
    HANDLE m_processHandle = nullptr;
    HANDLE m_inputWrite = nullptr; // ConPTY's stdin: we write, ConPTY reads
    HANDLE m_outputRead = nullptr; // ConPTY's stdout+stderr: ConPTY writes, we read

    // Reading and writing each get their own thread rather than sharing
    // one: the reader spends its whole life blocked in ReadFile and never
    // returns to an event loop, so a write queued onto that same thread
    // would sit unserved until the pipe happened to produce output. The
    // writer is a normal Qt worker-on-a-QThread (its event loop dispatches
    // write() calls as they're queued); the reader overrides run() to
    // block instead, since it has nothing else to do.
    QThread *m_readThread = nullptr;
    QThread *m_writeThread = nullptr;
    PtyReader *m_reader = nullptr;
    PtyWriter *m_writer = nullptr;

    QWinEventNotifier *m_exitNotifier = nullptr;
#else
    int m_masterFd = -1;
    pid_t m_childPid = -1;
    QSocketNotifier *m_notifier = nullptr;
#endif
};
