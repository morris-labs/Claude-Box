#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class TerminalWidget;
class QLabel;
class QPushButton;

// A small dialog holding one live TerminalWidget, for any one-off command
// worth watching (and, for an interactive login, typing into) rather than
// running silently in the background -- an `ssh`/`ssh-copy-id` login
// prompt, `docker build` output. Not tied to a box/container; see
// TerminalWidget::attachToCommand().
//
// The command starts as soon as the dialog is constructed and keeps
// running regardless of whether the dialog is shown modally or not.
// Closing the dialog (or destroying it) tears down the PTY, which sends
// the child SIGTERM (see PtySession's destructor) -- so closing early
// does abandon whatever the command was doing, same as Ctrl+C would.
class CommandTerminalDialog : public QDialog {
    Q_OBJECT
public:
    CommandTerminalDialog(const QString &title, const QString &program,
                          const QStringList &args, QWidget *parent = nullptr);

    // Valid once sessionEnded() has fired; -1 until then (or if the
    // process never managed to start at all).
    int exitCode() const { return m_exitCode; }
    bool hasFinished() const { return m_finished; }

signals:
    void sessionEnded(int exitCode);

private:
    TerminalWidget *m_terminal = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_closeButton = nullptr;
    bool m_finished = false;
    int m_exitCode = -1;
};
