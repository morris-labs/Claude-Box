#pragma once

#include "BoxRecord.h"
#include <QList>
#include <QSet>

// Allocates port blocks from a reserved range so every new box gets a set of
// pre-mapped host ports without manual bookkeeping. The next-free pointer
// advances after each allocation and wraps when it reaches the top of the
// range; ports already held by a known BoxRecord are skipped during the scan,
// so a wrap never hands out a port another box still holds.
//
// Allocated ports are persisted as "HOST:CONTAINER" pairs (same port on both
// sides) in BoxRecord::ports and survive until the box is purged.
class PortAllocator {
public:
    // 20000-20999: 1000 ports, enough for 200 boxes at 5 ports each, well
    // clear of common dev-server ports (3000, 4000, 5000, 8000, 8080, 9000).
    static constexpr int kRangeStart = 20000;
    static constexpr int kRangeEnd   = 20999;

    // Like allocate() (removed), but does NOT advance the stored next-base pointer.
    // Use this to pre-fill a dialog: call tentativeAllocate() before
    // exec(), then call setNextBase() only after the user clicks Accept.
    // This way cancelling the dialog does not permanently consume the ports.
    static QList<int> tentativeAllocate(int count, const QList<BoxRecord> &existing);
    static void setNextBase(int port);

private:
    static QSet<int> reservedPorts(const QList<BoxRecord> &existing);
    static int  nextBase();
};
