#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// A minimal synchronous client for the Docker Engine API over its unix
// socket: enough to GET a JSON endpoint, and deliberately nothing else.
//
// Why this exists when everything else here shells out to the `docker`
// CLI: `docker stats --no-stream` costs ~2 SECONDS per call, because it
// waits for a second CPU sample before it prints anything. The same
// numbers come back from the API in about a millisecond (measured on the
// dev machine: 1977ms vs 1ms) if you ask for one shot and compute the
// delta yourself from consecutive polls. That endpoint is polled every
// few seconds for as long as the app is open, so it is worth talking to
// the daemon directly.
//
// Container lifecycle goes through here too, which matters most for the
// Windows port: there the CLI is a Docker Desktop shim that costs
// hundreds of milliseconds per invocation, and process creation itself is
// far more expensive than on Linux. Talking to the daemon avoids both.
// The CLI remains the fallback for any setup this can't reach (see
// socketPath()), so a tcp:// or ssh:// DOCKER_HOST still works.
//
// Qt has no HTTP-over-unix-socket client (QNetworkAccessManager cannot do
// it at all), so this speaks just enough HTTP/1.1 over a QLocalSocket --
// which is also why it ports: QLocalSocket is a unix socket on Unix and a
// named pipe on Windows, and the docker daemon listens on both.
namespace DockerApi {

// The API version this client pins. The daemon accepts anything from its
// MinAPIVersion up; pinning means a newer daemon can't change a response
// shape under us.
extern const char *kApiVersion;

// unix:///var/run/docker.sock by default, or whatever DOCKER_HOST names
// -- but only when DOCKER_HOST is a unix:// socket. Anything else (a
// tcp:// daemon, an ssh:// context) returns empty, which makes
// isAvailable() false and sends every caller down the CLI path, where
// that configuration is handled properly.
QString socketPath();

// Whether a GET is worth attempting: the socket exists and accepts a
// connection. Cheap, and cached after the first successful check.
bool isAvailable();

// GETs path (e.g. "/v1.44/containers/json"), parses the body as JSON.
// Returns a null document on any failure -- connection, HTTP status, or
// malformed body -- with the reason in errorOut when given.
QJsonDocument get(const QString &path, QString *errorOut = nullptr, int timeoutMs = 5000);

// POSTs `body` as application/json (send an empty object for endpoints
// that take no body). Returns the parsed response, which is a null
// document both for an empty 204 and for a failure -- check errorOut,
// which is only set on failure.
QJsonDocument post(const QString &path, const QJsonObject &body, QString *errorOut = nullptr,
                   int timeoutMs = 30000);

// DELETEs path. Returns false with errorOut set on failure.
bool del(const QString &path, QString *errorOut = nullptr, int timeoutMs = 15000);

} // namespace DockerApi
