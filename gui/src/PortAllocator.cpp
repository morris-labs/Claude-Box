#include "PortAllocator.h"

#include <QSettings>

namespace {
const char *kNextBaseKey = "portAllocator/nextBase";
}

QSet<int> PortAllocator::reservedPorts(const QList<BoxRecord> &existing)
{
    QSet<int> used;
    for (const BoxRecord &rec : existing) {
        for (const QString &p : rec.ports) {
            const int colon = p.indexOf(':');
            bool ok = false;
            const int hostPort = (colon >= 0 ? p.left(colon) : p).toInt(&ok);
            if (ok && hostPort > 0)
                used.insert(hostPort);
        }
    }
    return used;
}

int PortAllocator::nextBase()
{
    QSettings s;
    const int stored = s.value(kNextBaseKey, kRangeStart).toInt();
    // Clamp in case a previous build used a different range.
    return (stored >= kRangeStart && stored <= kRangeEnd) ? stored : kRangeStart;
}

void PortAllocator::setNextBase(int port)
{
    QSettings s;
    s.setValue(kNextBaseKey, port);
}

QList<int> PortAllocator::allocate(int count, const QList<BoxRecord> &existing)
{
    if (count <= 0)
        return {};

    const QSet<int> reserved = reservedPorts(existing);
    const int rangeSize = kRangeEnd - kRangeStart + 1;

    int candidate = nextBase();
    int checked = 0;

    while (checked < rangeSize) {
        // A block that would overflow the range can't be used; wrap to start.
        if (candidate + count - 1 > kRangeEnd) {
            const int skipped = kRangeEnd - candidate + 1;
            checked += skipped;
            candidate = kRangeStart;
            continue;
        }

        // Find the first reserved port in [candidate, candidate+count).
        int conflict = -1;
        for (int i = 0; i < count; ++i) {
            if (reserved.contains(candidate + i)) {
                conflict = i;
                break;
            }
        }

        if (conflict < 0) {
            QList<int> result;
            result.reserve(count);
            for (int i = 0; i < count; ++i)
                result.append(candidate + i);
            int next = candidate + count;
            if (next > kRangeEnd)
                next = kRangeStart;
            setNextBase(next);
            return result;
        }

        // Skip past the conflict and try again.
        checked   += conflict + 1;
        candidate += conflict + 1;
        if (candidate > kRangeEnd)
            candidate = kRangeStart;
    }

    return {};  // entire range is occupied
}
