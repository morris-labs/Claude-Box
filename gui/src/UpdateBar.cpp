#include "UpdateBar.h"

#include "Theme.h"

#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>

namespace {
const QString kDismissedKey = QStringLiteral("updateChecker/dismissedVersion");
// Amber tone: reads as informational, not an error.
const char *kBarStyle =
    "background: #3a3020; border-bottom: 1px solid #7a6030; padding: 4px 8px;";
const char *kLinkStyle =
    "color: #d9aa57; text-decoration: underline;";
}

UpdateBar::UpdateBar(QWidget *parent)
    : QWidget(parent)
{
    setVisible(false);
    setStyleSheet(QLatin1String(kBarStyle));

    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;").arg(Theme::text().name()));

    m_actionLink = new QLabel(this);
    m_actionLink->setStyleSheet(QLatin1String(kLinkStyle));
    m_actionLink->setCursor(Qt::PointingHandCursor);
    m_actionLink->setVisible(false);
    m_actionLink->installEventFilter(this);

    auto *dismiss = new QPushButton(QStringLiteral("×"), this);
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

    connect(dismiss, &QPushButton::clicked, this, [this] {
        QSettings s;
        const QString ver = property("_dismissedVersion").toString();
        if (!ver.isEmpty())
            s.setValue(kDismissedKey, ver);
        setVisible(false);
    });
}

void UpdateBar::notify(const QString &newVersion, const QUrl &releaseUrl)
{
    QSettings s;
    if (s.value(kDismissedKey).toString() == newVersion)
        return;

    setProperty("_dismissedVersion", newVersion);
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
    setProperty("_dismissedVersion", QString());
    m_action = std::move(action);
    m_releaseUrl = QUrl();

    m_messageLabel->setText(message);
    m_actionLink->setText(actionLabel);
    m_actionLink->setVisible(true);
    setVisible(true);
}

bool UpdateBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_actionLink && event->type() == QEvent::MouseButtonPress) {
        if (m_action)
            m_action();
        else if (m_releaseUrl.isValid())
            QDesktopServices::openUrl(m_releaseUrl);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}
