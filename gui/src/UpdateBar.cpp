#include "UpdateBar.h"

#include "Theme.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

namespace {
const QString kDismissedKey = QStringLiteral("updateChecker/dismissedVersion");
}

UpdateBar::UpdateBar(QWidget *parent)
    : QWidget(parent)
{
    setVisible(false);
    setStyleSheet(QStringLiteral(
        "background: %1; border-bottom: 1px solid %2; padding: 4px 8px;")
        .arg(Theme::updateBarBg().name(), Theme::updateBarBorder().name()));

    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;").arg(Theme::text().name()));

    m_actionLink = new QPushButton(this);
    m_actionLink->setFlat(true);
    m_actionLink->setStyleSheet(
        QStringLiteral("QPushButton { color: %1; text-decoration: underline;"
                       " background: transparent; border: none; padding: 0px; }")
            .arg(Theme::accent().name()));
    m_actionLink->setCursor(Qt::PointingHandCursor);
    m_actionLink->setVisible(false);

    auto *dismiss = new QPushButton(QString(QChar(0x00D7)), this);
    dismiss->setFlat(true);
    dismiss->setFixedSize(20, 20);
    dismiss->setStyleSheet(
        QStringLiteral("color: %1; background: transparent; font-size: 14px; border: none;")
            .arg(Theme::dimText().name()));
    dismiss->setToolTip(QStringLiteral("Dismiss"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(8);
    layout->addWidget(m_messageLabel, 1);
    layout->addWidget(m_actionLink);
    layout->addWidget(dismiss);

    connect(m_actionLink, &QPushButton::clicked, this, [this] {
        if (m_action)
            m_action();
        else if (m_releaseUrl.isValid())
            QDesktopServices::openUrl(m_releaseUrl);
    });

    connect(dismiss, &QPushButton::clicked, this, [this] {
        if (!m_dismissedVersion.isEmpty()) {
            QSettings s;
            s.setValue(kDismissedKey, m_dismissedVersion);
        }
        setVisible(false);
    });
}

void UpdateBar::notify(const QString &newVersion, const QUrl &releaseUrl)
{
    QSettings s;
    if (s.value(kDismissedKey).toString() == newVersion)
        return;

    m_dismissedVersion = newVersion;
    m_releaseUrl = releaseUrl;
    m_action = nullptr;

    m_messageLabel->setText(
        QStringLiteral("Version %1 of claude-box is available.").arg(newVersion));
    m_actionLink->setText(QStringLiteral("View release"));
    m_actionLink->setVisible(true);
    setVisible(true);
}

void UpdateBar::notifyWithAction(const QString &message,
                                 const QString &actionLabel,
                                 std::function<void()> action)
{
    m_dismissedVersion.clear();
    m_action = std::move(action);
    m_releaseUrl = QUrl();

    m_messageLabel->setText(message);
    m_actionLink->setText(actionLabel);
    m_actionLink->setVisible(!actionLabel.isEmpty());
    setVisible(true);
}
