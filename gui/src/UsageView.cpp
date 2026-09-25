#include "UsageView.h"

#include "DockerBackend.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// Rounded pill-style bar: no border, subtle track, filled chunk with
// rounded ends. Less blocky than the previous bordered rectangle.
QString progressBarStyle(const QString &chunkColor)
{
    return QStringLiteral(
        "QProgressBar {"
        "  border: none;"
        "  background: %1;"
        "  border-radius: 5px;"
        "  text-align: center;"
        "  color: white;"
        "}"
        "QProgressBar::chunk {"
        "  background: %2;"
        "  border-radius: 5px;"
        "}")
        .arg(Theme::panelBg().name(), chunkColor);
}

// Color shifts from green at low load to red at high load.
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
    bar->setFixedHeight(20);
    bar->setTextVisible(true);
    return bar;
}

// Row label with consistent dim styling.
QLabel *makeBarLabel(const QString &text, QWidget *parent)
{
    auto *lbl = new QLabel(text, parent);
    lbl->setFixedWidth(44);
    lbl->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    return lbl;
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
    outer->setContentsMargins(14, 12, 14, 12);
    outer->setSpacing(10);

    // ---- totals section ------------------------------------------------
    auto *totalGrid = new QVBoxLayout();
    totalGrid->setSpacing(8);

    auto addTotalRow = [&](const QString &label, QProgressBar *&bar, const QString &color) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        row->addWidget(makeBarLabel(label, this));
        bar = makeBar(color, this);
        row->addWidget(bar, 1);
        totalGrid->addLayout(row);
    };

    addTotalRow(QStringLiteral("CPU"), m_totalCpuBar, QColor(45, 160, 80).name());
    addTotalRow(QStringLiteral("MEM"), m_totalMemBar, QColor(80, 130, 200).name());

    // Disk I/O totals (no fill bar -- unbounded cumulative bytes).
    {
        auto *diskRow = new QHBoxLayout();
        diskRow->setSpacing(8);
        diskRow->addWidget(makeBarLabel(QStringLiteral("Disk"), this));
        m_totalDiskLabel = new QLabel(QStringLiteral("—"), this);
        m_totalDiskLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(Theme::text().name()));
        diskRow->addWidget(m_totalDiskLabel, 1);
        totalGrid->addLayout(diskRow);
    }

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
    m_rowsLayout->setSpacing(14);
    m_rowsLayout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(m_rowsWidget);
    outer->addWidget(scroll, 1);

    // Show placeholder immediately; rebuild() replaces it when boxes appear.
    auto *none = new QLabel(QStringLiteral("No running boxes."), m_rowsWidget);
    none->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    none->setAlignment(Qt::AlignCenter);
    m_rowsLayout->insertWidget(0, none);
}

void UsageView::setBoxes(const QList<BoxInfo> &boxes)
{
    rebuild(boxes);
}

void UsageView::updateTotals(const QList<BoxInfo> &boxes)
{
    double totalCpu = 0.0;
    quint64 totalMemUsed = 0, totalMemLimit = 0;
    quint64 totalDiskRead = 0, totalDiskWrite = 0;
    int cpuSampled = 0;

    for (const BoxInfo &b : boxes) {
        if (b.status != BoxInfo::Status::Running)
            continue;
        if (b.cpuPct >= 0.0f) {
            totalCpu += b.cpuPct;
            ++cpuSampled;
        }
        totalMemUsed  += b.memUsedBytes;
        // Each container's memLimitBytes is the host RAM when no --memory
        // limit is set, so summing would give N × host RAM as the denominator.
        // Use the max instead: when all containers share the same host RAM
        // limit, this gives the actual available memory as the scale.
        totalMemLimit  = qMax(totalMemLimit, b.memLimitBytes);
        totalDiskRead  += b.diskReadBytes;
        totalDiskWrite += b.diskWriteBytes;
    }

    if (cpuSampled > 0) {
        const int cpuDisplay = int(qBound(0.0, totalCpu, 100.0 * cpuSampled));
        m_totalCpuBar->setRange(0, 100 * qMax(cpuSampled, 1));
        m_totalCpuBar->setValue(cpuDisplay);
        const double frac = double(cpuDisplay) / double(100 * qMax(cpuSampled, 1));
        const QString cpuStyle = progressBarStyle(barChunkColor(frac));
        if (cpuStyle != m_lastTotalCpuStyle) {
            m_totalCpuBar->setStyleSheet(cpuStyle);
            m_lastTotalCpuStyle = cpuStyle;
        }
        m_totalCpuBar->setFormat(QStringLiteral("%1%").arg(int(totalCpu + 0.5)));
    } else {
        m_totalCpuBar->setRange(0, 100);
        m_totalCpuBar->setValue(0);
        m_totalCpuBar->setFormat(QStringLiteral("—"));
    }

    if (totalMemLimit > 0) {
        const int memPct = int(double(totalMemUsed) / double(totalMemLimit) * 100.0 + 0.5);
        m_totalMemBar->setValue(qBound(0, memPct, 100));
        const double frac = qBound(0.0, double(totalMemUsed) / double(totalMemLimit), 1.0);
        const QString memStyle = progressBarStyle(barChunkColor(frac));
        if (memStyle != m_lastTotalMemStyle) {
            m_totalMemBar->setStyleSheet(memStyle);
            m_lastTotalMemStyle = memStyle;
        }
        m_totalMemBar->setFormat(QStringLiteral("%1%  (%2 / %3)")
            .arg(memPct).arg(DockerBackend::humanBytes(totalMemUsed),
                             DockerBackend::humanBytes(totalMemLimit)));
    } else {
        m_totalMemBar->setValue(0);
        m_totalMemBar->setFormat(QStringLiteral("—"));
    }

    if (cpuSampled > 0) {
        // Stats API returned data; show disk even if zero (valid on cgroupsv2).
        m_totalDiskLabel->setText(
            QStringLiteral("R: %1  W: %2")
                .arg(DockerBackend::humanBytes(totalDiskRead),
                     DockerBackend::humanBytes(totalDiskWrite)));
    } else {
        m_totalDiskLabel->setText(QStringLiteral("—"));
    }
}

void UsageView::updateRow(const QString &name, const BoxInfo &b)
{
    auto it = m_rowCache.find(name);
    if (it == m_rowCache.end())
        return;
    RowWidgets &rw = it.value();

    // Refresh label in case conversationName changed since the row was created.
    if (rw.nameLabel) {
        const QString label = b.conversationName.isEmpty() ? b.name : b.conversationName;
        if (rw.nameLabel->text() != label)
            rw.nameLabel->setText(label);
    }

    if (b.cpuPct >= 0.0f) {
        const QString cpuStyle = progressBarStyle(barChunkColor(b.cpuPct / 100.0));
        if (cpuStyle != rw.lastCpuStyle) {
            rw.cpuBar->setStyleSheet(cpuStyle);
            rw.lastCpuStyle = cpuStyle;
        }
        rw.cpuBar->setValue(int(qBound(0.0f, b.cpuPct, 100.0f)));
        rw.cpuBar->setFormat(QStringLiteral("%1%").arg(int(b.cpuPct + 0.5f)));
    } else {
        rw.cpuBar->setValue(0);
        rw.cpuBar->setFormat(QStringLiteral("—"));
    }

    const double memFrac = b.memLimitBytes > 0
        ? qBound(0.0, double(b.memUsedBytes) / double(b.memLimitBytes), 1.0)
        : 0.0;
    const QString memStyle = progressBarStyle(barChunkColor(memFrac));
    if (memStyle != rw.lastMemStyle) {
        rw.memBar->setStyleSheet(memStyle);
        rw.lastMemStyle = memStyle;
    }
    if (b.memLimitBytes > 0) {
        const int memPct = int(memFrac * 100.0 + 0.5);
        rw.memBar->setValue(memPct);
        rw.memBar->setFormat(QStringLiteral("%1%  (%2)")
            .arg(memPct).arg(DockerBackend::humanBytes(b.memUsedBytes)));
    } else {
        rw.memBar->setValue(0);
        rw.memBar->setFormat(QStringLiteral("—"));
    }

    if (rw.diskLabel) {
        // Show values whenever stats are available (cpuPct >= 0 means
        // the stats API returned data). Zero bytes is valid on cgroupsv2.
        if (b.cpuPct >= 0.0f) {
            rw.diskLabel->setText(
                QStringLiteral("R: %1  W: %2")
                    .arg(DockerBackend::humanBytes(b.diskReadBytes),
                         DockerBackend::humanBytes(b.diskWriteBytes)));
        } else {
            rw.diskLabel->setText(QStringLiteral("—"));
        }
    }
}

void UsageView::rebuild(const QList<BoxInfo> &boxes)
{
    // Build the new running set: map box name -> BoxInfo.
    QMap<QString, const BoxInfo *> newRunning;
    for (const BoxInfo &b : boxes) {
        if (b.status == BoxInfo::Status::Running)
            newRunning[b.name] = &b;
    }

    const QStringList newNames = newRunning.keys(); // sorted
    const QStringList oldNames = m_rowCache.keys(); // sorted

    if (newNames != oldNames) {
        // Running set changed -- rebuild all per-box rows from scratch.
        const int n = m_rowsLayout->count();
        if (n > 0) {
            QLayoutItem *stretch = m_rowsLayout->takeAt(n - 1);
            delete stretch;
        }
        clearLayout(m_rowsLayout);
        m_rowCache.clear();

        for (const QString &name : newNames) {
            const BoxInfo &b = *newRunning[name];
            auto *card = new QWidget(m_rowsWidget);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(0, 0, 0, 0);
            cardLayout->setSpacing(5);

            // Name heading.
            auto *nameLabel = new QLabel(
                b.conversationName.isEmpty() ? b.name : b.conversationName, card);
            nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            QFont boldFont = nameLabel->font();
            boldFont.setBold(true);
            nameLabel->setFont(boldFont);
            nameLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::text().name()));
            cardLayout->addWidget(nameLabel);

            // CPU bar row.
            auto *cpuRow = new QHBoxLayout();
            cpuRow->setSpacing(8);
            cpuRow->addWidget(makeBarLabel(QStringLiteral("CPU"), card));
            auto *cpuBar = makeBar(QColor(45, 160, 80).name(), card);
            cpuRow->addWidget(cpuBar, 1);
            cardLayout->addLayout(cpuRow);

            // MEM bar row.
            auto *memRow = new QHBoxLayout();
            memRow->setSpacing(8);
            memRow->addWidget(makeBarLabel(QStringLiteral("MEM"), card));
            auto *memBar = makeBar(barChunkColor(0.0), card);
            memRow->addWidget(memBar, 1);
            cardLayout->addLayout(memRow);

            // Disk I/O row.
            auto *diskRow = new QHBoxLayout();
            diskRow->setSpacing(8);
            diskRow->addWidget(makeBarLabel(QStringLiteral("Disk"), card));
            auto *diskLabel = new QLabel(QStringLiteral("—"), card);
            diskLabel->setStyleSheet(
                QStringLiteral("color: %1;").arg(Theme::text().name()));
            diskRow->addWidget(diskLabel, 1);
            cardLayout->addLayout(diskRow);

            m_rowsLayout->addWidget(card);

            RowWidgets rw;
            rw.container = card;
            rw.nameLabel = nameLabel;
            rw.cpuBar    = cpuBar;
            rw.memBar    = memBar;
            rw.diskLabel = diskLabel;
            m_rowCache[name] = rw;

            updateRow(name, b);
        }

        if (newNames.isEmpty()) {
            auto *none = new QLabel(QStringLiteral("No running boxes."), m_rowsWidget);
            none->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
            none->setAlignment(Qt::AlignCenter);
            m_rowsLayout->addWidget(none);
        }

        m_rowsLayout->addStretch(1);
    } else {
        // Running set unchanged -- update bar values in place.
        for (const QString &name : newNames)
            updateRow(name, *newRunning[name]);
    }

    updateTotals(boxes);
}
