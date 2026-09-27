#pragma once
#include <QDialog>

class QPlainTextEdit;
class QTabWidget;

// A dialog for viewing and editing the three prompt templates that
// DockerBackend sends to newly created boxes: workspace subfolder,
// port hint, and starter. Changes are saved to QSettings on OK.
class PromptsDialog : public QDialog {
    Q_OBJECT
public:
    explicit PromptsDialog(QWidget *parent = nullptr);

private:
    void accept() override;

    QPlainTextEdit *makeTab(QTabWidget *tabs,
                            const QString &title,
                            const QString &description,
                            const QString &placeholderHelp,
                            const QString &current,
                            const QString &defaultText);

    QPlainTextEdit *m_workspaceEdit = nullptr;
    QPlainTextEdit *m_portHintEdit  = nullptr;
    QPlainTextEdit *m_starterEdit   = nullptr;
};
