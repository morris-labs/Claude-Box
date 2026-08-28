#include "BoxDetailsPanel.h"

#include "BoxRecord.h"
#include "Icons.h"
#include "Theme.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

QColor statusColor(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running: return Theme::running();
    case BoxInfo::Status::Stopped: return Theme::stopped();
    case BoxInfo::Status::Known:   return Theme::known();
    }
    return Theme::known();
}

QString statusText(BoxInfo::Status s)
{
    switch (s) {
    case BoxInfo::Status::Running: return QStringLiteral("Running");
    case BoxInfo::Status::Stopped: return QStringLiteral("Stopped");
    case BoxInfo::Status::Known:   return QStringLiteral("Not running");
    }
    return QString();
}

// Placeholder for a field the record doesn't carry, so an empty value is
// visibly "nothing set" rather than looking like a rendering bug.
const QString kNone = QStringLiteral("—");

} // namespace

BoxDetailsPanel::BoxDetailsPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    auto *content = new QWidget(m_stack);
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
    m_sessionUuid  = addField(fieldsLayout, "Session");
    m_flags        = addField(fieldsLayout, "Flags");
    m_ports        = addField(fieldsLayout, "Ports");
    m_mounts       = addField(fieldsLayout, "Mounts");
    m_detail       = addField(fieldsLayout, "Docker");

    // The uuid is the field most likely to be copied out (to hand to
    // `claude --resume` by hand), so it gets the monospace treatment.
    m_sessionUuid->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    contentLayout->addWidget(m_fields);

    contentLayout->addStretch(1);

    m_placeholder = new QLabel(QStringLiteral("Select a box to see its details."), m_stack);
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setWordWrap(true);
    m_placeholder->setStyleSheet(QString("color: %1;").arg(Theme::dimText().name()));

    m_stack->addWidget(m_placeholder); // index 0
    m_stack->addWidget(content);       // index 1

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

void BoxDetailsPanel::setBox(const BoxInfo *info)
{
    const bool have = info != nullptr;
    m_stack->setCurrentIndex(have ? 1 : 0);
    if (!have)
        return;

    m_heading->setText(info->conversationName.isEmpty() ? info->name : info->conversationName);

    const QColor color = statusColor(info->status);
    m_statusDot->setPixmap(Icons::statusDot(color).pixmap(12, 12));
    m_statusText->setText(statusText(info->status));
    m_statusText->setStyleSheet(QString("color: %1; font-weight: 600;").arg(color.name()));

    m_conversation->setText(info->name);
    m_directory->setText(info->targetDir.isEmpty() ? kNone : info->targetDir);
    m_detail->setText(info->detail.isEmpty() ? kNone : info->detail);

    // Record-backed fields. A box running outside this app has no record,
    // so these stay blank rather than showing another box's settings.
    const BoxRecord rec = BoxRecord::load(info->name);
    if (!rec.isValid()) {
        m_sessionUuid->setText(kNone);
        m_flags->setText(QStringLiteral("no tracked record"));
        m_ports->setText(kNone);
        m_mounts->setText(kNone);
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
}
