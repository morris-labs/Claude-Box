#include "PtySession.h"

#include <QSocketNotifier>
#ifdef __APPLE__
#  include <QDir>
#  include <QStandardPaths>
#endif

#include <cerrno>
#include <cstring>
#include <vector>

#include <fcntl.h>
#ifdef Q_OS_DARWIN
#  include <util.h>   // macOS ships forkpty() here
#else
#  include <pty.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

PtySession::PtySession(QObject *parent)
    : QObject(parent)
{
}

PtySession::~PtySession()
{
    if (m_childPid > 0)
        ::kill(m_childPid, SIGTERM);
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    if (m_childPid > 0) {
        // SIGTERM was sent above; the master fd close also delivered SIGHUP to
        // the slave. Give the child one non-blocking check, then SIGKILL it so
        // waitpid returns promptly. `docker exec` cleaning up a container
        // attachment can take arbitrarily long on SIGTERM alone, which blocks
        // the GUI on every close when any terminal tab is open.
        if (::waitpid(m_childPid, nullptr, WNOHANG) == 0) {
            ::kill(m_childPid, SIGKILL);
            ::waitpid(m_childPid, nullptr, 0);
        }
        m_childPid = -1;
    }
    // m_notifier is a QObject child of `this`; Qt destroys it automatically.
}

bool PtySession::isRunning() const
{
    return m_masterFd >= 0;
}

bool PtySession::start(const QString &program, const QStringList &args, const QString &workingDir)
{
    if (isRunning())
        return false;

    // Build everything the child needs to exec *before* forking. Only
    // async-signal-safe calls (chdir, execvp, _exit) should run between
    // fork() and exec() in a process that may have Qt/other background
    // threads -- QString/QByteArray allocation is not guaranteed safe
    // there, so none of it happens after forkpty() below.
    //
    // progBytes is non-const so the macOS path-resolution block below can
    // update it. argv[0] is set after that block so it never holds a
    // pointer into a freed QByteArray.
    QByteArray progBytes = program.toLocal8Bit();
    std::vector<QByteArray> argBytesStorage;
    argBytesStorage.reserve(static_cast<size_t>(args.size()));
    for (const QString &a : args)
        argBytesStorage.push_back(a.toLocal8Bit());

    std::vector<char *> argv;
    argv.reserve(argBytesStorage.size() + 2);
    argv.push_back(nullptr); // argv[0]: set after macOS path resolution below
    for (auto &b : argBytesStorage)
        argv.push_back(b.data());
    argv.push_back(nullptr);

    const QByteArray workingDirBytes = workingDir.toLocal8Bit();

    struct winsize ws {};
    ws.ws_row = 24;
    ws.ws_col = 80;

#ifdef __APPLE__
    // A .app bundle launched via `open` inherits only /usr/bin:/bin:/usr/sbin:/sbin.
    // setenv() is not async-signal-safe (it may call malloc and deadlock in the
    // child of a multithreaded process). Build the modified environment and
    // resolve the program path here in the parent instead, then pass them via
    // execve() in the child.
    std::vector<QByteArray> childEnvStorage;
    std::vector<char *> childEnvp;
    {
        const char *home = ::getenv("HOME");
        const char *cur  = ::getenv("PATH");

        // Build the extra-dirs prefix using QByteArray so long HOME values
        // are not silently truncated (a fixed-size snprintf buffer would do
        // that without any indication).
        QByteArray extra =
            "/usr/local/bin:/opt/homebrew/bin"
            ":/Applications/Docker.app/Contents/Resources/bin";
        if (home && *home) {
            extra += ':';
            extra += home;
            extra += "/.docker/bin";
        }

        QByteArray newPath = "PATH=";
        newPath += extra;
        if (cur && *cur) {
            newPath += ':';
            newPath += cur;
        }

        bool pathReplaced = false;
        for (char **ep = ::environ; *ep; ++ep) {
            if (::strncmp(*ep, "PATH=", 5) == 0) {
                childEnvStorage.push_back(newPath);
                pathReplaced = true;
            } else {
                childEnvStorage.push_back(QByteArray(*ep));
            }
        }
        if (!pathReplaced)
            childEnvStorage.push_back(newPath);

        // Build the pointer array after all push_backs so no reallocation
        // can invalidate the pointers before we hand them to execve().
        childEnvp.reserve(childEnvStorage.size() + 1);
        for (auto &b : childEnvStorage)
            childEnvp.push_back(b.data());
        childEnvp.push_back(nullptr);
    }

    // execve() does not search PATH, so a bare program name needs a full path.
    // Resolve it here in the parent against the same extended directory set.
    if (!progBytes.contains('/')) {
        const QStringList extraDirs = {
            QStringLiteral("/usr/local/bin"),
            QStringLiteral("/opt/homebrew/bin"),
            QStringLiteral("/Applications/Docker.app/Contents/Resources/bin"),
            QDir::homePath() + QStringLiteral("/.docker/bin"),
        };
        QString found = QStandardPaths::findExecutable(program, extraDirs);
        if (found.isEmpty())
            found = QStandardPaths::findExecutable(program);
        if (!found.isEmpty())
            progBytes = found.toLocal8Bit();
        // If still not found, execve will return ENOENT and the child exits
        // 127 -- the terminal widget shows "Session ended (exit 127)".
    }
    // Set argv[0] here, after progBytes has its final value. Setting it
    // earlier and then reassigning progBytes would leave argv[0] dangling.
    argv[0] = const_cast<char *>(progBytes.constData());
#else
    // Non-Apple: set argv[0] now (no path resolution needed here).
    argv[0] = const_cast<char *>(progBytes.constData());
#endif

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, &ws);
    if (pid < 0)
        return false;

    if (pid == 0) {
        // Child: fds 0/1/2 are already the pty slave and it's already our
        // controlling terminal (forkpty() handles setsid()/TIOCSCTTY).
        if (!workingDirBytes.isEmpty() && ::chdir(workingDirBytes.constData()) != 0)
            ::_exit(127); // better to fail visibly than exec in the wrong directory
#ifdef __APPLE__
        // childEnvp and the resolved argv[0] were built in the parent; use
        // execve() so no PATH search or environment mutation happens here.
        ::execve(argv[0], argv.data(), childEnvp.data());
#else
        ::execvp(argv[0], argv.data());
#endif
        ::_exit(127); // only reached if exec failed
    }

    // Parent.
    m_masterFd = master;
    m_childPid = pid;

    const int flags = ::fcntl(m_masterFd, F_GETFL, 0);
    ::fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &PtySession::onMasterReadable);

    return true;
}

void PtySession::onMasterReadable()
{
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(m_masterFd, buf, sizeof(buf));
        if (n > 0) {
            emit dataReady(QByteArray(buf, static_cast<int>(n)));
            if (n < static_cast<ssize_t>(sizeof(buf)))
                break; // drained for now, wait for the next activation
            continue;
        }
        if (n == 0) {
            cleanup(); // EOF: remote side closed
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        if (errno == EINTR)
            continue;
        // EIO on Linux is the normal way a pty master reports that the
        // slave (and thus our child) is gone.
        cleanup();
        break;
    }
}

void PtySession::write(const QByteArray &data)
{
    if (m_masterFd < 0)
        return;

    const char *p = data.constData();
    qint64 remaining = data.size();
    while (remaining > 0) {
        const ssize_t n = ::write(m_masterFd, p, static_cast<size_t>(remaining));
        if (n > 0) {
            p += n;
            remaining -= n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        // EAGAIN (master is non-blocking) or a real error: drop the rest
        // rather than block the UI thread. Fine for keystroke-sized
        // writes; a large paste could theoretically be truncated under
        // heavy backpressure.
        break;
    }
}

void PtySession::resize(int rows, int cols)
{
    if (m_masterFd < 0)
        return;
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    ::ioctl(m_masterFd, TIOCSWINSZ, &ws);
}

void PtySession::cleanup()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }

    int exitCode = -1;
    if (m_childPid > 0) {
        int status = 0;
        // The child is already dead or dying by the time we see EOF/EIO,
        // so this just reaps it rather than actually blocking.
        if (::waitpid(m_childPid, &status, 0) > 0 && WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
        m_childPid = -1;
    }

    emit finished(exitCode);
}
