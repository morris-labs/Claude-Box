#include "PromptsDialog.h"
#include "PromptTemplates.h"
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

PromptsDialog::PromptsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Edit prompt templates"));
    resize(720, 520);

    auto *tabs = new QTabWidget(this);

    m_workspaceEdit = makeTab(tabs,
        QStringLiteral("Workspace subfolder"),
        QStringLiteral("Sent as the opening message when a new box is created with a workspace "
                       "subfolder. The agent receives this before any user input."),
        QStringLiteral("Placeholders: %1 = folder slug,  %2 = conversation name,  %3 = workspace folder path,  "
                       "%4 = this conversation's notes directory,  %5 = the shared INFRA.md path"),
        PromptTemplates::workspace(),
        PromptTemplates::defaultWorkspace());

    m_portHintEdit = makeTab(tabs,
        QStringLiteral("Port hint"),
        QStringLiteral("Appended to the opening message of any new box that has mapped ports. "
                       "Tells the agent which container-side ports it can bind for services "
                       "the host needs to reach."),
        QStringLiteral("Placeholder: %1 = comma-separated list of mapped ports"),
        PromptTemplates::portHint(),
        PromptTemplates::defaultPortHint());

    m_starterEdit = makeTab(tabs,
        QStringLiteral("Starter"),
        QStringLiteral("Sent as the opening message for new boxes that have no workspace "
                       "subfolder. Clear the field to send no opening message. Port hints are "
                       "appended after this when ports are also mapped."),
        QStringLiteral("Placeholders: %1 = conversation name,  %2 = target directory path"),
        PromptTemplates::starter(),
        PromptTemplates::defaultStarter());

    m_worktreeEdit = makeTab(tabs,
        QStringLiteral("Worktree"),
        QStringLiteral("Appended to the workspace message when a new box works in its own "
                       "git worktree. Tells the agent which branch it is on and to leave the "
                       "main checkout alone."),
        QStringLiteral("Placeholders: %1 = repository path,  %2 = branch,  %3 = worktree path"),
        PromptTemplates::worktree(),
        PromptTemplates::defaultWorktree());

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PromptsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PromptsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

void PromptsDialog::accept()
{
    PromptTemplates::setWorkspace(m_workspaceEdit->toPlainText());
    PromptTemplates::setPortHint(m_portHintEdit->toPlainText());
    PromptTemplates::setStarter(m_starterEdit->toPlainText());
    PromptTemplates::setWorktree(m_worktreeEdit->toPlainText());
    QDialog::accept();
}

QPlainTextEdit *PromptsDialog::makeTab(QTabWidget *tabs,
                                        const QString &title,
                                        const QString &description,
                                        const QString &placeholderHelp,
                                        const QString &current,
                                        const QString &defaultText)
{
    auto *page   = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setSpacing(6);

    auto *descLabel = new QLabel(description, page);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    auto *edit = new QPlainTextEdit(current, page);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(edit);

    auto *helpLabel = new QLabel(placeholderHelp, page);
    QPalette p = helpLabel->palette();
    p.setColor(QPalette::WindowText, p.color(QPalette::Disabled, QPalette::WindowText));
    helpLabel->setPalette(p);
    layout->addWidget(helpLabel);

    auto *resetRow    = new QHBoxLayout;
    auto *resetButton = new QPushButton(QStringLiteral("Reset to default"), page);
    resetRow->addStretch();
    resetRow->addWidget(resetButton);
    layout->addLayout(resetRow);

    connect(resetButton, &QPushButton::clicked, edit, [edit, defaultText] {
        edit->setPlainText(defaultText);
    });

    tabs->addTab(page, title);
    return edit;
}
