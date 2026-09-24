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
        totalMemLimit = qMax(totalMemLimit, b.memLimitBytes);
    }

    if (cpuSampled > 0) {
        const int cpuDisplay = int(qBound(0.0, totalCpu, 100.0 * cpuSampled));
        m_totalCpuBar->setRange(0, 100 * qMax(cpuSampled, 1));
        m_totalCpuBar->setValue(cpuDisplay);
        const double frac = double(cpuDisplay) / double(100 * qMax(cpuSampled, 1));
        m_totalCpuBar->setStyleSheet(progressBarStyle(barChunkColor(frac)));
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
        m_totalMemBar->setStyleSheet(progressBarStyle(barChunkColor(frac)));
        m_totalMemBar->setFormat(QStringLiteral("%1  (%2 / %3)")
            .arg(memPct).arg(DockerBackend::humanBytes(totalMemUsed),
                             DockerBackend::humanBytes(totalMemLimit)));
    } else {
        m_totalMemBar->setValue(0);
        m_totalMemBar->setFormat(QStringLiteral("—"));
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
        rw.cpuBar->setStyleSheet(progressBarStyle(barChunkColor(b.cpuPct / 100.0)));
        rw.cpuBar->setValue(int(qBound(0.0f, b.cpuPct, 100.0f)));
        rw.cpuBar->setFormat(QStringLiteral("%1%").arg(int(b.cpuPct + 0.5f)));
    } else {
        rw.cpuBar->setValue(0);
        rw.cpuBar->setFormat(QStringLiteral("—"));
    }

    const double memFrac = b.memLimitBytes > 0
        ? qBound(0.0, double(b.memUsedBytes) / double(b.memLimitBytes), 1.0)
        : 0.0;
    rw.memBar->setStyleSheet(progressBarStyle(barChunkColor(memFrac)));
    if (b.memLimitBytes > 0) {
        const int memPct = int(memFrac * 100.0 + 0.5);
        rw.memBar->setValue(memPct);
        rw.memBar->setFormat(QStringLiteral("%1  (%2)")
            .arg(memPct).arg(DockerBackend::humanBytes(b.memUsedBytes)));
    } else {
        rw.memBar->setValue(0);
        rw.memBar->setFormat(QStringLiteral("—"));
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
        // Remove the trailing stretch before clearing the layout.
        const int n = m_rowsLayout->count();
        if (n > 0) {
            QLayoutItem *stretch = m_rowsLayout->takeAt(n - 1);
            delete stretch;
        }
        clearLayout(m_rowsLayout);
        m_rowCache.clear();

        for (const QString &name : newNames) {
            const BoxInfo &b = *newRunning[name];
            auto *row = new QWidget(m_rowsWidget);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 2, 0, 2);
            rowLayout->setSpacing(6);

            auto *nameLabel = new QLabel(b.conversationName.isEmpty() ? b.name : b.conversationName, row);
            nameLabel->setFixedWidth(200);
            nameLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::text().name()));
            nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            rowLayout->addWidget(nameLabel);

            auto *cpuBar = makeBar(QColor(45, 160, 80).name(), row);
            rowLayout->addWidget(cpuBar, 2);

            auto *memBar = makeBar(barChunkColor(0.0), row);
            rowLayout->addWidget(memBar, 3);

            m_rowsLayout->addWidget(row);

            RowWidgets rw;
            rw.container = row;
            rw.nameLabel = nameLabel;
            rw.cpuBar    = cpuBar;
            rw.memBar    = memBar;
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
