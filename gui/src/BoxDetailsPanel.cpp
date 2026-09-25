#include "BoxDetailsPanel.h"

#include "BoxRecord.h"
#include "CollapsibleSection.h"
#include "DockerBackend.h"
#include "Icons.h"
#include "Theme.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QColor statusColor(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running: return Theme::running();
    case BoxInfo::Status::Stopped: return Theme::known();
    case BoxInfo::Status::Exited:  return Theme::stopped();
    }
    return Theme::known();
}

QString statusText(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running: return QStringLiteral("Running");
    case BoxInfo::Status::Stopped: return QStringLiteral("Not running");
    case BoxInfo::Status::Exited:  return QStringLiteral("Exited");
    }
    return QString();
}

// Placeholder for a field the record doesn't carry, so an empty value is
// visibly "nothing set" rather than looking like a rendering bug.
const QString kNone = QStringLiteral("—");

// Mirrors NewBoxDialog's own forwardLabel() -- kept as a separate copy
// (like the slugify() duplication elsewhere in this app) since this side
// only ever needs it for one read-only line, not worth a shared header for.
QString forwardLabel(const QString &value)
{
    const QStringList parts = value.split(':');
    if (parts.size() != 5)
        return value;
    const QString dirLabel = parts.at(0) == QLatin1String("R") ? QStringLiteral("remote") : QStringLiteral("local");
    const QString bindLabel = parts.at(1).isEmpty() ? parts.at(2) : (parts.at(1) + ":" + parts.at(2));
    return QStringLiteral("%1 %2 → %3:%4").arg(dirLabel, bindLabel, parts.at(3), parts.at(4));
}


QProgressBar *makeStatsBar(QWidget *parent)
{
    auto *bar = new QProgressBar(parent);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setStyleSheet(Theme::progressBarStyle(QColor(45, 160, 80).name()));
    bar->setFixedHeight(20);
    bar->setTextVisible(true);
    bar->setFormat(QStringLiteral("—"));
    return bar;
}

// QLayout::takeAt() hands back one QLayoutItem and does not touch anything
// underneath it: a widget item's widget stays alive (parented to the
// container, not the item) and a nested layout added via addLayout() -- as
// the per-remote rows and the Reconnect row below are -- keeps its own
// child widgets. So the widgets have to be deleted explicitly, and a
// nested layout has to be recursed into first or its rows leak as
// orphaned-but-still-visible widgets that pile up and overlap on every
// rebuild.
//
// The nested layout itself is freed by `delete item` alone, NOT by a
// separate `delete item->layout()`: for a sub-layout QBoxLayout wraps it
// in an internal item whose destructor already deletes the layout, so
// deleting both double-frees it and corrupts the heap (a later, unrelated
// allocation then crashes). Recurse to clear it, then delete only the item.
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

BoxDetailsPanel::BoxDetailsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(14, 12, 14, 12);
    contentLayout->setSpacing(10);

    m_heading = new QLabel(this);
    QFont headingFont = m_heading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() + 1.5);
    headingFont.setBold(true);
    m_heading->setFont(headingFont);
    m_heading->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_heading->setWordWrap(true);
    contentLayout->addWidget(m_heading);

    auto *statusRow = new QHBoxLayout();
    statusRow->setSpacing(6);
    m_statusDot = new QLabel(this);
    m_statusText = new QLabel(this);
    statusRow->addWidget(m_statusDot);
    statusRow->addWidget(m_statusText);
    statusRow->addStretch(1);
    contentLayout->addLayout(statusRow);

    m_fields = new QWidget(this);
    // Caption stacked above value in a plain vertical layout, rather than a
    // QFormLayout. A form's field column stays narrow in a side panel this
    // width, and long values (container names, paths, uuids) were wrapping
    // to a second line the row was never given the height to show -- so
    // they simply lost their tails. A vertical layout hands each value the
    // panel's full width, which is exactly what these values want.
    auto *fieldsLayout = new QVBoxLayout(m_fields);
    fieldsLayout->setContentsMargins(0, 4, 0, 0);
    fieldsLayout->setSpacing(2);

    m_conversation = addField(fieldsLayout, "Container");
    m_directory    = addField(fieldsLayout, "Directory");

    // Shown only when the recorded directory is missing on disk (externally
    // moved or deleted). Hidden otherwise.
    m_relinkButton = new QPushButton(QStringLiteral("Relink directory…"), m_fields);
    m_relinkButton->setToolTip(
        QStringLiteral("Pick the new location of this directory to update the record"));
    m_relinkButton->setVisible(false);
    {
        auto *btnRow = new QHBoxLayout();
        btnRow->addWidget(m_relinkButton);
        btnRow->addStretch(1);
        fieldsLayout->addLayout(btnRow);
    }
    connect(m_relinkButton, &QPushButton::clicked, this, [this] {
        const QString newDir = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("Locate directory for '%1'").arg(m_currentBoxName),
            QDir::homePath());
        if (!newDir.isEmpty())
            emit relinkRequested(m_currentBoxName, newDir);
    });

    m_sessionUuid  = addField(fieldsLayout, "Session");
    m_flags        = addField(fieldsLayout, "Flags");
    m_ports        = addField(fieldsLayout, "Ports");
    m_mounts       = addField(fieldsLayout, "Mounts");

    // "SSH forwards" used to be addField()'s plain QLabel like the fields
    // above -- now a caption plus a rebuildable container, so each remote
    // can carry its own connection-status dot instead of one flat block of
    // text with no way to tell which remote (if any) is actually down.
    auto *sshCaption = new QLabel("SSH forwards", m_fields);
    QFont sshCaptionFont = sshCaption->font();
    sshCaptionFont.setPointSizeF(sshCaptionFont.pointSizeF() - 0.5);
    sshCaption->setFont(sshCaptionFont);
    sshCaption->setStyleSheet(QString("color: %1;").arg(Theme::dimText().name()));
    fieldsLayout->addSpacing(8);
    fieldsLayout->addWidget(sshCaption);

    m_sshContainer = new QWidget(m_fields);
    m_sshLayout = new QVBoxLayout(m_sshContainer);
    m_sshLayout->setContentsMargins(0, 2, 0, 0);
    m_sshLayout->setSpacing(4);
    fieldsLayout->addWidget(m_sshContainer);

    m_detail       = addField(fieldsLayout, "Docker");

    // The uuid is the field most likely to be copied out (to hand to
    // `claude --resume` by hand), so it gets the monospace treatment.
    m_sessionUuid->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    contentLayout->addWidget(m_fields);

    // Collapsed by default -- cpu/mem is ambient info refreshed every poll,
    // not usually what this panel gets opened to check.
    m_statsSection = new CollapsibleSection("Resource Usage", this);
    auto *statsLayout = new QVBoxLayout();
    statsLayout->setContentsMargins(0, 4, 0, 0);
    statsLayout->setSpacing(7);

    auto addStatsRow = [&](const QString &label, QWidget *widget) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *lbl = new QLabel(label, this);
        lbl->setFixedWidth(44);
        lbl->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
        row->addWidget(lbl);
        row->addWidget(widget, 1);
        statsLayout->addLayout(row);
    };

    m_cpuBar = makeStatsBar(this);
    addStatsRow(QStringLiteral("CPU"), m_cpuBar);

    m_memBar = makeStatsBar(this);
    m_memBar->setStyleSheet(Theme::progressBarStyle(QColor(80, 130, 200).name()));
    addStatsRow(QStringLiteral("MEM"), m_memBar);

    m_diskLabel = new QLabel(QStringLiteral("—"), this);
    m_diskLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::text().name()));
    addStatsRow(QStringLiteral("Disk"), m_diskLabel);

    m_statsSection->setContentLayout(statsLayout);
    contentLayout->addWidget(m_statsSection);

    contentLayout->addStretch(1);

    m_placeholder = new QLabel(QStringLiteral("Select a box to see its details."), m_stack);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setStyleSheet(QString("color: %1;").arg(Theme::dimText().name()));

    // Scrollable rather than a fixed-height page: a box with several SSH
    // remotes each carrying a handful of forwards (see BoxRecord's own
    // sshRemotes) can easily run past this panel's height, and the point
    // of a side panel is to never bunch or clip a value rather than to
    // fit everything on one screen unscrolled.
    auto *scrollArea = new QScrollArea(m_stack);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(content);

    m_stack->addWidget(m_placeholder); // index 0
    m_stack->addWidget(scrollArea);    // index 1

    setBox(nullptr);
}

QLabel *BoxDetailsPanel::addField(QVBoxLayout *layout, const QString &label)
{
    auto *caption = new QLabel(label, m_fields);
    QFont captionFont = caption->font();
    captionFont.setPointSizeF(captionFont.pointSizeF() - 0.5);
    caption->setFont(captionFont);
    caption->setStyleSheet(QString("color: %1;").arg(Theme::dimText().name()));

    auto *value = new QLabel(m_fields);
    value->setWordWrap(true);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value->setMinimumWidth(0);

    if (layout->count() > 0)
        layout->addSpacing(8);
    layout->addWidget(caption);
    layout->addWidget(value);
    return value;
}

void BoxDetailsPanel::setBox(const BoxInfo *info, const QList<SshRemote> &sshRemotes,
                              const QList<SshTunnelStatus> &tunnelStatuses)
{
    const bool have = info != nullptr;
    m_stack->setCurrentIndex(have ? 1 : 0);
    if (!have)
        return;

    m_currentBoxName = info->name;

    m_heading->setText(info->conversationName.isEmpty() ? info->name : info->conversationName);

    const QColor color = statusColor(info->status);
    m_statusDot->setPixmap(Icons::statusDot(color).pixmap(12, 12));
    m_statusText->setText(statusText(info->status));
    m_statusText->setStyleSheet(QString("color: %1; font-weight: 600;").arg(color.name()));

    m_conversation->setText(info->name);

    const bool dirMissing = !info->targetDir.isEmpty()
                            && !QFileInfo::exists(info->targetDir);
    if (info->targetDir.isEmpty()) {
        m_directory->setText(kNone);
        m_directory->setStyleSheet(QString());
    } else if (dirMissing) {
        m_directory->setText(info->targetDir + QStringLiteral("  [missing]"));
        m_directory->setStyleSheet(
            QStringLiteral("color: %1;").arg(Theme::stopped().name()));
    } else {
        m_directory->setText(info->targetDir);
        m_directory->setStyleSheet(QString());
    }
    // A Running box still has its old path bind-mounted even if the host
    // side of that path is temporarily unreachable (a flaky network mount,
    // say) -- relinking then would silently make the dashboard show a path
    // the live container isn't actually using. Match Move/Change Working
    // Directory's own guard: relinking is disabled, not hidden, while
    // Running, so the missing-path warning stays visible either way.
    const bool relinkable = dirMissing && info->status != BoxInfo::Status::Running;
    m_relinkButton->setVisible(dirMissing);
    m_relinkButton->setEnabled(relinkable);
    m_relinkButton->setToolTip(
        info->status == BoxInfo::Status::Running
            ? QStringLiteral("Stop the box before relinking its directory -- "
                              "it's still bind-mounted to the current path")
            : QStringLiteral("Pick the new location of this directory to update the record"));

    m_detail->setText(info->detail.isEmpty() ? kNone : info->detail);

    // Resource usage bars -- populated from the numeric BoxInfo fields so
    // they render as actual bars rather than just text.
    if (info->cpuPct >= 0.0f) {
        const double frac = qBound(0.0, double(info->cpuPct) / 100.0, 1.0);
        const QString style = Theme::progressBarStyle(Theme::barChunkColor(frac));
        if (style != m_lastCpuStyle) {
            m_cpuBar->setStyleSheet(style);
            m_lastCpuStyle = style;
        }
        m_cpuBar->setValue(int(qBound(0.0f, info->cpuPct, 100.0f)));
        m_cpuBar->setFormat(QStringLiteral("%1%").arg(int(info->cpuPct + 0.5f)));
    } else {
        m_cpuBar->setValue(0);
        m_cpuBar->setFormat(QStringLiteral("—"));
    }

    if (info->memLimitBytes > 0) {
        const double frac = qBound(0.0, double(info->memUsedBytes) / double(info->memLimitBytes), 1.0);
        const QString style = Theme::progressBarStyle(Theme::barChunkColor(frac));
        if (style != m_lastMemStyle) {
            m_memBar->setStyleSheet(style);
            m_lastMemStyle = style;
        }
        const int memPct = int(frac * 100.0 + 0.5);
        m_memBar->setValue(memPct);
        m_memBar->setFormat(QStringLiteral("%1%  (%2 / %3)")
            .arg(memPct)
            .arg(DockerBackend::humanBytes(info->memUsedBytes),
                 DockerBackend::humanBytes(info->memLimitBytes)));
    } else {
        m_memBar->setValue(0);
        m_memBar->setFormat(QStringLiteral("—"));
    }

    if (info->diskTotalBytes > 0) {
        m_diskLabel->setText(
            QStringLiteral("Rw: %1  Total: %2")
                .arg(DockerBackend::humanBytes(info->diskRwBytes),
                     DockerBackend::humanBytes(info->diskTotalBytes)));
    } else {
        m_diskLabel->setText(QStringLiteral("—"));
    }

    // Record-backed fields. A box running outside this app has no record,
    // so these stay blank rather than showing another box's settings.
    const BoxRecord rec = BoxRecord::load(info->name);
    if (!rec.isValid()) {
        m_sessionUuid->setText(kNone);
        m_flags->setText(QStringLiteral("no tracked record"));
        m_ports->setText(kNone);
        m_mounts->setText(kNone);
        rebuildSshSection({}, {});
        return;
    }

    m_sessionUuid->setText(rec.sessionUuid.isEmpty() ? kNone : rec.sessionUuid);

    // --remote-control isn't listed: it's passed to every box
    // unconditionally, so saying so per row is noise.
    QStringList flags;
    if (rec.skipPermissions)
        flags << "--dangerously-skip-permissions";
    if (!rec.model.isEmpty())
        flags << ("--model " + rec.model);
    if (!rec.workspaceDir.isEmpty())
        flags << ("workspace " + rec.workspaceDir + "/");
    if (!rec.effort.isEmpty())
        flags << ("--effort " + rec.effort);
    m_flags->setText(flags.isEmpty() ? QStringLiteral("none") : flags.join(", "));

    m_ports->setText(rec.ports.isEmpty() ? kNone : rec.ports.join("\n"));
    m_mounts->setText(rec.dirs.isEmpty() ? kNone : rec.dirs.join("\n"));

    rebuildSshSection(sshRemotes, tunnelStatuses);
}

void BoxDetailsPanel::rebuildSshSection(const QList<SshRemote> &remotes,
                                         const QList<SshTunnelStatus> &tunnelStatuses)
{
    // Rebuilt from scratch each call -- see the member declaration for why.
    clearLayout(m_sshLayout);

    bool anyConfigured = false;
    for (int i = 0; i < remotes.size(); ++i) {
        const SshRemote &remote = remotes.at(i);
        if (remote.forwards.isEmpty() || remote.host.trimmed().isEmpty())
            continue;
        anyConfigured = true;

        const SshTunnelStatus status = tunnelStatuses.value(i); // default: not attempted

        auto *row = new QHBoxLayout();
        row->setSpacing(6);

        auto *dot = new QLabel(m_sshContainer);
        // Dim when we haven't tried this remote at all -- typically because
        // the box isn't Running, so MainWindow isn't tunneling it. Once
        // attempted, green means the ssh process is currently alive and
        // red means it isn't (auth failure, unreachable host, exited and
        // hasn't been retried yet -- see SshTunnelSession's ExitOnForward-
        // Failure). This is a proxy for "tunnel up", not a guarantee: ssh
        // -N prints nothing on success, so a live process is the closest
        // signal available short of probing the forwarded port ourselves.
        const QColor dotColor = !status.attempted ? Theme::dimText()
            : (status.running ? Theme::running() : Theme::stopped());
        dot->setPixmap(Icons::statusDot(dotColor).pixmap(10, 10));
        if (status.attempted && !status.running && !status.lastError.isEmpty())
            dot->setToolTip(status.lastError);
        row->addWidget(dot);

        const QString hostText = remote.name.isEmpty() ? remote.host
            : QStringLiteral("%1 (%2)").arg(remote.name, remote.host);
        auto *hostLabel = new QLabel(QStringLiteral("via %1").arg(hostText), m_sshContainer);
        hostLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (status.attempted && !status.running && !status.lastError.isEmpty())
            hostLabel->setToolTip(status.lastError);
        row->addWidget(hostLabel);
        row->addStretch(1);
        m_sshLayout->addLayout(row);

        for (const QString &fwd : remote.forwards) {
            auto *fwdLabel = new QLabel(QStringLiteral("  ") + forwardLabel(fwd), m_sshContainer);
            fwdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            fwdLabel->setWordWrap(true);
            m_sshLayout->addWidget(fwdLabel);
        }
    }

    if (!anyConfigured) {
        m_sshLayout->addWidget(new QLabel(kNone, m_sshContainer));
        return;
    }

    // No point offering to reconnect tunnels the box wasn't given any
    // status for -- MainWindow only reports statuses for a Running box.
    if (!tunnelStatuses.isEmpty()) {
        auto *reconnect = new QPushButton("Reconnect", m_sshContainer);
        reconnect->setToolTip("Tear down and restart every SSH tunnel for this box");
        connect(reconnect, &QPushButton::clicked, this, [this] {
            emit reconnectRequested(m_currentBoxName);
        });
        auto *btnRow = new QHBoxLayout();
        btnRow->addWidget(reconnect);
        btnRow->addStretch(1);
        m_sshLayout->addLayout(btnRow);
    }
}
