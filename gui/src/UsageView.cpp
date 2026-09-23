#include "UsageView.h"

#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// Shared QProgressBar stylesheet: flat bar with no native chrome, using the
// dark theme colors so it reads as one app with the rest of the UI.
QString progressBarStyle(const QString &chunkColor)
{
    return QStringLiteral(
        "QProgressBar {"
        "  border: 1px solid %1;"
        "  background: %2;"
        "  text-align: center;"
        "  color: white;"
        "}"
        "QProgressBar::chunk {"
        "  background: %3;"
        "}")
        .arg(Theme::border().name(),
             Theme::baseBg().name(),
             chunkColor);
}

// Color shifts from green at low load to red at high load (matching UsageBarDelegate).
QString barChunkColor(double fraction)
{
    const QColor green(45, 160, 80);
    const QColor red(200, 80, 50);
    QColor c = fraction <= 0.6 ? green
               : QColor(int(green.red()   + (red.red()   - green.red())   * (fraction - 0.6) / 0.4),
                        int(green.green() + (red.green() - green.green()) * (fraction - 0.6) / 0.4),
                        int(green.blue()  + (red.blue()  - green.blue())  * (fraction - 0.6) / 0.4));
    return c.name();
}

QProgressBar *makeBar(const QString &chunkColor, QWidget *parent)
{
    auto *bar = new QProgressBar(parent);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setStyleSheet(progressBarStyle(chunkColor));
    bar->setFixedHeight(18);
    bar->setTextVisible(true);
    return bar;
}

// Human-readable byte count, matching DockerBackend's own humanBytes().
QString humanBytes(quint64 b)
{
    if (b < 1024)           return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024)    return QStringLiteral("%1 KiB").arg(b / 1024);
    if (b < 1024ull * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(b / (1024 * 1024));
    return QStringLiteral("%1 GiB").arg(double(b) / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

void clearLayout(QLayout *layout)
{
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget())
            delete w;
        else if (QLayout *child = item->layout())
            clearLayout(child);
        delete item;
    }
}

} // namespace

UsageView::UsageView(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(6);

    // ---- totals section ------------------------------------------------
    auto *totalGrid = new QVBoxLayout();
    totalGrid->setSpacing(4);

    auto addTotalRow = [&](const QString &label, QProgressBar *&bar, const QString &color) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *lbl = new QLabel(label, this);
        lbl->setFixedWidth(80);
        lbl->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
        bar = makeBar(color, this);
        row->addWidget(lbl);
        row->addWidget(bar, 1);
        totalGrid->addLayout(row);
    };

    addTotalRow(QStringLiteral("Total CPU"), m_totalCpuBar, QColor(45, 160, 80).name());
    addTotalRow(QStringLiteral("Total MEM"), m_totalMemBar, QColor(80, 130, 200).name());
    outer->addLayout(totalGrid);

    // Separator between totals and per-box rows.
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::border().name()));
    outer->addWidget(sep);

    // ---- per-box rows (scrollable) -------------------------------------
    m_rowsWidget = new QWidget(this);
    m_rowsLayout = new QVBoxLayout(m_rowsWidget);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(4);
    m_rowsLayout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_rowsWidget);
    outer->addWidget(scroll, 1);
}

void UsageView::setBoxes(const QList<BoxInfo> &boxes)
{
    rebuild(boxes);
}

void UsageView::rebuild(const QList<BoxInfo> &boxes)
{
    // ---- compute totals ------------------------------------------------
    double totalCpu = 0.0;
    quint64 totalMemUsed = 0, totalMemLimit = 0;
    int cpuSampled = 0;

    for (const BoxInfo &b : boxes) {
        if (b.status != BoxInfo::Status::Running)
            continue;
        if (b.cpuPct >= 0.0f) {
            totalCpu += b.cpuPct;
            ++cpuSampled;
        }
        totalMemUsed  += b.memUsedBytes;
        totalMemLimit += b.memLimitBytes;
    }

    // Update total bars. Cap CPU display at 100 * running-box-count.
    if (cpuSampled > 0) {
        const int cpuDisplay = int(qBound(0.0, totalCpu, 100.0 * cpuSampled));
        m_totalCpuBar->setRange(0, 100 * qMax(cpuSampled, 1));
        m_totalCpuBar->setValue(cpuDisplay);
        const double frac = double(cpuDisplay) / double(100 * qMax(cpuSampled, 1));
        m_totalCpuBar->setStyleSheet(progressBarStyle(barChunkColor(frac)));
        m_totalCpuBar->setFormat(QStringLiteral("%1%").arg(int(totalCpu + 0.5)));
    } else {
        m_totalCpuBar->setValue(0);
        m_totalCpuBar->setFormat(QStringLiteral("—"));
    }

    if (totalMemLimit > 0) {
        const int memPct = int(double(totalMemUsed) / double(totalMemLimit) * 100.0 + 0.5);
        m_totalMemBar->setValue(qBound(0, memPct, 100));
        const double frac = qBound(0.0, double(totalMemUsed) / double(totalMemLimit), 1.0);
        m_totalMemBar->setStyleSheet(progressBarStyle(barChunkColor(frac)));
        m_totalMemBar->setFormat(QStringLiteral("%1  (%2 / %3)")
            .arg(memPct).arg(humanBytes(totalMemUsed), humanBytes(totalMemLimit)));
    } else {
        m_totalMemBar->setValue(0);
        m_totalMemBar->setFormat(QStringLiteral("—"));
    }

    // ---- per-box rows --------------------------------------------------
    // Remove old stretch, rebuild rows, re-add stretch.
    // The stretch is always the last item; remove it first.
    {
        const int n = m_rowsLayout->count();
        if (n > 0) {
            QLayoutItem *stretch = m_rowsLayout->takeAt(n - 1);
            delete stretch;
        }
    }
    clearLayout(m_rowsLayout);

    for (const BoxInfo &b : boxes) {
        if (b.status != BoxInfo::Status::Running)
            continue;

        auto *row = new QWidget(m_rowsWidget);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 2, 0, 2);
        rowLayout->setSpacing(6);

        // Name label, fixed width so bars align across rows.
        auto *nameLabel = new QLabel(b.conversationName.isEmpty() ? b.name : b.conversationName,
                                     row);
        nameLabel->setFixedWidth(200);
        nameLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::text().name()));
        nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        rowLayout->addWidget(nameLabel);

        // CPU bar.
        auto *cpuBar = makeBar(b.cpuPct >= 0 ? barChunkColor(b.cpuPct / 100.0)
                                              : QColor(45, 160, 80).name(), row);
        if (b.cpuPct >= 0.0f) {
            cpuBar->setValue(int(qBound(0.0f, b.cpuPct, 100.0f)));
            cpuBar->setFormat(QStringLiteral("%1%").arg(int(b.cpuPct + 0.5f)));
        } else {
            cpuBar->setValue(0);
            cpuBar->setFormat(QStringLiteral("—"));
        }
        rowLayout->addWidget(cpuBar, 2);

        // Memory bar.
        const double memFrac = b.memLimitBytes > 0
            ? qBound(0.0, double(b.memUsedBytes) / double(b.memLimitBytes), 1.0)
            : 0.0;
        auto *memBar = makeBar(barChunkColor(memFrac), row);
        if (b.memLimitBytes > 0) {
            const int memPct = int(memFrac * 100.0 + 0.5);
            memBar->setValue(memPct);
            memBar->setFormat(QStringLiteral("%1  (%2)")
                .arg(memPct).arg(humanBytes(b.memUsedBytes)));
        } else {
            memBar->setValue(0);
            memBar->setFormat(QStringLiteral("—"));
        }
        rowLayout->addWidget(memBar, 3);

        m_rowsLayout->addWidget(row);
    }

    // Placeholder when no boxes are running.
    bool anyRunning = false;
    for (const BoxInfo &b : boxes)
        if (b.status == BoxInfo::Status::Running) { anyRunning = true; break; }
    if (!anyRunning) {
        auto *none = new QLabel(QStringLiteral("No running boxes."), m_rowsWidget);
        none->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
        none->setAlignment(Qt::AlignCenter);
        m_rowsLayout->addWidget(none);
    }

    m_rowsLayout->addStretch(1);
}
