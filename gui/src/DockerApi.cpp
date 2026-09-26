#include "DockerApi.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcessEnvironment>

#include <atomic>

namespace {

// Reads until the socket closes or the deadline passes. The requests here
// all send "Connection: close", so a clean disconnect is the end of the
// body and there's no need to interpret Content-Length just to know when
// to stop.
QByteArray readAll(QLocalSocket &socket, int timeoutMs)
{
    QByteArray data;
    QElapsedTimer clock;
    clock.start();

    while (socket.state() == QLocalSocket::ConnectedState) {
        const int remaining = timeoutMs - int(clock.elapsed());
        if (remaining <= 0)
            break;
        if (!socket.waitForReadyRead(remaining))
            break; // disconnect or timeout; either way there is no more
        data += socket.readAll();
    }
    data += socket.readAll(); // whatever landed with the disconnect
    return data;
}

// Chunked bodies come back from the stats endpoint, so this has to be
// handled rather than assumed away.
QByteArray dechunk(const QByteArray &body)
{
    QByteArray out;
    int pos = 0;
    while (pos < body.size()) {
        const int lineEnd = body.indexOf("\r\n", pos);
        if (lineEnd < 0)
            break;
        // A chunk header may carry extensions after a ';'.
        QByteArray sizeField = body.mid(pos, lineEnd - pos);
        const int semi = sizeField.indexOf(';');
        if (semi >= 0)
            sizeField = sizeField.left(semi);

        bool ok = false;
        const int size = sizeField.trimmed().toInt(&ok, 16);
        if (!ok)
            break;
        if (size == 0)
            break; // last chunk

        out += body.mid(lineEnd + 2, size);
        pos = lineEnd + 2 + size + 2; // chunk data + its trailing CRLF
    }
    return out;
}

} // namespace

const char *DockerApi::kApiVersion = "v1.44";

QString DockerApi::socketPath()
{
    const QString host = QProcessEnvironment::systemEnvironment().value("DOCKER_HOST");

    if (host.startsWith("unix://"))
        return host.mid(7);
    // QLocalSocket takes a pipe *name*, not the \\.\pipe\ path.
    if (host.startsWith("npipe:////./pipe/"))
        return host.mid(17);
    if (!host.isEmpty())
        return QString(); // tcp://, ssh:// -- the CLI handles those properly

#ifdef Q_OS_WIN
    // Docker Desktop's default endpoint. QLocalSocket speaks named pipes
    // on Windows, so the rest of this file needs no #ifdef at all.
    return QStringLiteral("docker_engine");
#elif defined(Q_OS_DARWIN)
    // Docker Desktop for Mac creates the socket here in recent versions.
    // /var/run/docker.sock is a legacy symlink that may be absent or
    // require elevated permissions, so probe the newer path first.
    const QString primary = QDir::homePath() + QStringLiteral("/.docker/run/docker.sock");
    if (QFileInfo::exists(primary))
        return primary;
    return QStringLiteral("/var/run/docker.sock");
#else
    return QStringLiteral("/var/run/docker.sock");
#endif
}

bool DockerApi::isAvailable()
{
    // Re-checked only until it first succeeds: a daemon that is there
    // stays there, and a missing socket is worth retrying in case docker
    // gets started while the app is open. Called from both the GUI thread
    // and the worker thread listBoxes() runs on, so this needs to be an
    // atomic rather than a plain bool.
    static std::atomic<bool> known{false};
    if (known.load(std::memory_order_relaxed))
        return true;

    const QString path = socketPath();
    if (path.isEmpty())
        return false;
    // Only a real filesystem socket can be stat'd; a Windows pipe name
    // isn't a path, so there the connect attempt below is the whole test.
    if (path.startsWith('/') && !QFileInfo::exists(path))
        return false;

    QLocalSocket socket;
    socket.connectToServer(path);
    const bool connected = socket.waitForConnected(1000);
    socket.abort();
    if (connected)
        known.store(true, std::memory_order_relaxed);
    return connected;
}

namespace {
// One request/response exchange. Every verb goes through here so that
// status handling, chunked bodies and error extraction exist once.
QJsonDocument request(const QByteArray &method, const QString &path, const QByteArray &body,
                      QString *errorOut, int timeoutMs)
{
    const QString socketFile = DockerApi::socketPath();
    if (socketFile.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("no local docker socket to talk to");
        return {};
    }

    QLocalSocket socket;
    socket.connectToServer(socketFile);
    if (!socket.waitForConnected(timeoutMs)) {
        if (errorOut)
            *errorOut = socket.errorString();
        return {};
    }

    QByteArray request = method + " " + path.toUtf8() + " HTTP/1.1\r\n"
                         "Host: docker\r\n"
                         "Accept: application/json\r\n"
                         "Connection: close\r\n";
    if (!body.isEmpty()) {
        request += "Content-Type: application/json\r\n"
                   "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    request += "\r\n" + body;
    socket.write(request);
    if (!socket.waitForBytesWritten(timeoutMs)) {
        if (errorOut)
            *errorOut = QStringLiteral("timed out sending request");
        return {};
    }

    const QByteArray response = readAll(socket, timeoutMs);
    socket.abort();

    const int split = response.indexOf("\r\n\r\n");
    if (split < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("truncated response");
        return {};
    }

    const QByteArray head = response.left(split);
    QByteArray payload = response.mid(split + 4);
    if (head.contains("Transfer-Encoding: chunked"))
        payload = dechunk(payload);

    // "HTTP/1.1 200 OK" -- anything but 2xx carries a JSON {"message":...}
    // that is more useful than the status line alone.
    const QList<QByteArray> statusParts = head.left(head.indexOf("\r\n")).split(' ');
    const int status = statusParts.size() > 1 ? statusParts.at(1).toInt() : 0;
    if (status < 200 || status > 299) {
        if (errorOut) {
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            const QString message = doc.object().value("message").toString();
            *errorOut = QStringLiteral("HTTP %1").arg(status)
                        + (message.isEmpty() ? QString() : (": " + message));
        }
        return {};
    }

    if (payload.trimmed().isEmpty())
        return {}; // 204 and friends: success with nothing to parse

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (doc.isNull() && errorOut)
        *errorOut = parseError.errorString();
    return doc;
}
} // namespace

QJsonDocument DockerApi::get(const QString &path, QString *errorOut, int timeoutMs)
{
    return request("GET", path, {}, errorOut, timeoutMs);
}

QJsonDocument DockerApi::post(const QString &path, const QJsonObject &body, QString *errorOut,
                              int timeoutMs)
{
    // An empty object still needs a body: some endpoints reject a POST
    // with no Content-Length.
    return request("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), errorOut,
                   timeoutMs);
}

bool DockerApi::del(const QString &path, QString *errorOut, int timeoutMs)
{
    QString error;
    request("DELETE", path, {}, &error, timeoutMs);
    if (!error.isEmpty()) {
        if (errorOut)
            *errorOut = error;
        return false;
    }
    return true;
}
