#pragma once

#include <functional>
#include <QUrl>
#include <QWidget>

class QLabel;
class QPushButton;

// A slim info bar shown when a newer version is available. Sits above the
// main content area; hidden by default. Call notify() to show it.
// The user can dismiss it; the dismissed version is remembered in QSettings
// so the bar doesn't reappear for the same release.
//
// notifyWithAction() is a general variant used by the Claude Code staleness
// check: it takes an arbitrary action callback and skips the dismiss-tracking.
class UpdateBar : public QWidget {
    Q_OBJECT
public:
    explicit UpdateBar(QWidget *parent = nullptr);

    // Shows the bar for `newVersion` linking to `releaseUrl`. No-op if the
    // user already dismissed this exact version.
    void notify(const QString &newVersion, const QUrl &releaseUrl);

    // Shows the bar with a custom message and action button. The action label
    // is shown as a clickable link; `action` is called when the user clicks it.
    // No dismiss tracking -- the caller manages visibility.
    void notifyWithAction(const QString &message, const QString &actionLabel,
                          std::function<void()> action);

private:
    QLabel      *m_messageLabel = nullptr;
    QPushButton *m_actionLink   = nullptr;
    std::function<void()> m_action;
    QUrl m_releaseUrl;
    QString m_dismissedVersion;
};
