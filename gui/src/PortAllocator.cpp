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

// Core allocation logic: compute a free block of `count` ports starting from
// `startBase`, skipping any in `reserved`. Returns the ports and the next
// base to use after this block, or an empty list when the range is full.
static QPair<QList<int>, int> computeAllocation(int count, const QSet<int> &reserved, int startBase)
{
    const int rangeSize = PortAllocator::kRangeEnd - PortAllocator::kRangeStart + 1;

    int candidate = startBase;
    int checked = 0;

    while (checked < rangeSize) {
        if (candidate + count - 1 > PortAllocator::kRangeEnd) {
            const int skipped = PortAllocator::kRangeEnd - candidate + 1;
            checked += skipped;
            candidate = PortAllocator::kRangeStart;
            continue;
        }

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
            if (next > PortAllocator::kRangeEnd)
                next = PortAllocator::kRangeStart;
            return {result, next};
        }

        checked   += conflict + 1;
        candidate += conflict + 1;
        if (candidate > PortAllocator::kRangeEnd)
            candidate = PortAllocator::kRangeStart;
    }

    return {{}, PortAllocator::kRangeStart};
}

QList<int> PortAllocator::tentativeAllocate(int count, const QList<BoxRecord> &existing)
{
    if (count <= 0)
        return {};
    return computeAllocation(count, reservedPorts(existing), nextBase()).first;
}
