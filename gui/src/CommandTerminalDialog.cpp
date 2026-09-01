#include "CommandTerminalDialog.h"

#include "TerminalWidget.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

CommandTerminalDialog::CommandTerminalDialog(const QString &title, const QString &program,
                                             const QStringList &args, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(720, 420);

    auto *layout = new QVBoxLayout(this);

    m_statusLabel = new QLabel(QStringLiteral("Running: %1 %2").arg(program, args.join(' ')), this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    layout->addWidget(m_statusLabel);

    m_terminal = new TerminalWidget(this);
    layout->addWidget(m_terminal, 1);

    m_closeButton = new QPushButton(QStringLiteral("Close"), this);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    buttonRow->addWidget(m_closeButton);
    layout->addLayout(buttonRow);

    connect(m_terminal, &TerminalWidget::sessionFinished, this, [this](int exitCode) {
        m_finished = true;
        m_exitCode = exitCode;
        m_statusLabel->setText(exitCode == 0
            ? QStringLiteral("Finished.")
            : QStringLiteral("Finished (exit %1).").arg(exitCode));
        emit sessionEnded(exitCode);
    });

    if (!m_terminal->attachToCommand(program, args)) {
        m_finished = true;
        m_exitCode = -1;
        m_statusLabel->setText(QStringLiteral("Failed to start %1.").arg(program));
    } else {
        m_terminal->setFocus();
    }
}
