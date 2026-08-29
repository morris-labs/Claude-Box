#include "ContainerPaths.h"

#include <QDir>
#include <QtGlobal>

#ifdef Q_OS_WIN

namespace {
// Qt normalizes path separators to forward slashes internally regardless
// of platform, so a Windows absolute path reads as "C:/Users/..." here,
// not "C:\Users\...".
bool looksLikeDriveLetterPath(const QString &path)
{
    return path.size() >= 2 && path.at(0).isLetter() && path.at(1) == QLatin1Char(':');
}
}

#endif

QString ContainerPaths::hostToContainer(const QString &hostPath)
{
    const QString absolute = QDir(hostPath).absolutePath();

#ifdef Q_OS_WIN
    if (looksLikeDriveLetterPath(absolute)) {
        const QChar drive = absolute.at(0).toLower();
        // Index 1 is the ':'; what follows is either nothing ("C:", the
        // drive root) or a '/' then the rest of the path.
        QString rest = absolute.mid(2);
        if (rest.startsWith(QLatin1Char('/')))
            rest.remove(0, 1);

        QString mapped = QStringLiteral("/mnt/host/") + drive;
        if (!rest.isEmpty())
            mapped += QLatin1Char('/') + rest;
        return mapped;
    }

    // No drive letter: most likely a UNC share, which Qt normalizes to
    // "//server/share/...". There's no drive letter to key off, so the
    // leading slashes fold into the same /mnt/host/ prefix rather than
    // being left as a bare "//" -- not something a container path can
    // sanely start with either. Untested against a real UNC target; flag
    // this case if it comes up during Windows bring-up.
    QString rest = absolute;
    while (rest.startsWith(QLatin1Char('/')))
        rest.remove(0, 1);
    return QStringLiteral("/mnt/host/unc/") + rest;
#else
    // Identical to what every call site did before this function existed:
    // the host path already is a legal, and correct, container path.
    return absolute;
#endif
}

void ContainerPaths::splitMountSpec(const QString &spec, QString &hostOut, QString &containerOut)
{
    int searchFrom = 0;
#ifdef Q_OS_WIN
    if (spec.size() >= 2 && spec.at(0).isLetter() && spec.at(1) == QLatin1Char(':'))
        searchFrom = 2; // that colon is the drive letter's, not the delimiter
#endif
    const int colon = spec.indexOf(QLatin1Char(':'), searchFrom);
    hostOut = colon < 0 ? spec : spec.left(colon);
    containerOut = colon < 0 ? QString() : spec.mid(colon + 1);
}
