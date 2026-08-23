#include "NewBoxDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// A labeled "list of strings you can add/remove from" block, used
// identically for port mappings and dir mounts below.
QListWidget *makeRepeatableListGroup(QVBoxLayout *parent, const QString &title,
                                      const QString &placeholder, QLineEdit **entryOut,
                                      QObject *receiver, const char *addSlot, const char *removeSlot)
{
    auto *group = new QGroupBox(title);
    auto *layout = new QVBoxLayout(group);

    auto *list = new QListWidget(group);
    layout->addWidget(list);

    auto *row = new QHBoxLayout();
    auto *entry = new QLineEdit(group);
    entry->setPlaceholderText(placeholder);
    auto *addButton = new QPushButton("Add", group);
    auto *removeButton = new QPushButton("Remove Selected", group);
    row->addWidget(entry);
    row->addWidget(addButton);
    row->addWidget(removeButton);
    layout->addLayout(row);

    QObject::connect(addButton, SIGNAL(clicked()), receiver, addSlot);
    QObject::connect(entry, SIGNAL(returnPressed()), receiver, addSlot);
    QObject::connect(removeButton, SIGNAL(clicked()), receiver, removeSlot);

    parent->addWidget(group);
    *entryOut = entry;
    return list;
}
}

NewBoxDialog::NewBoxDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("New Box");
    resize(480, 480);

    auto *mainLayout = new QVBoxLayout(this);

    auto *form = new QFormLayout();

    auto *dirRow = new QHBoxLayout();
    m_dirEdit = new QLineEdit(this);
    auto *browseButton = new QPushButton("Browse…", this);
    dirRow->addWidget(m_dirEdit);
    dirRow->addWidget(browseButton);
    form->addRow("Target directory:", dirRow);
    connect(browseButton, &QPushButton::clicked, this, &NewBoxDialog::browseForDir);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("optional -- used as-is, or as the issue name if new-issue.sh exists");
    form->addRow("Conversation name:", m_nameEdit);

    m_yoloCheck = new QCheckBox("Skip permission prompts (--yolo)", this);
    form->addRow(QString(), m_yoloCheck);

    m_rcCheck = new QCheckBox("Enable remote control (--rc)", this);
    form->addRow(QString(), m_rcCheck);

    mainLayout->addLayout(form);

    m_portList = makeRepeatableListGroup(mainLayout, "Port mappings", "HOST:CONTAINER",
                                          &m_portEntry, this, SLOT(addPort()), SLOT(removeSelectedPort()));

    m_dirList = makeRepeatableListGroup(mainLayout, "Extra dir mounts", "HOSTPATH[:CONTAINERPATH]",
                                         &m_dirEntry, this, SLOT(addDirMount()), SLOT(removeSelectedDirMount()));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewBoxDialog::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &NewBoxDialog::reject);
    mainLayout->addWidget(buttons);
}

void NewBoxDialog::setInitialDir(const QString &dir)
{
    m_dirEdit->setText(dir);
}

void NewBoxDialog::browseForDir()
{
    const QString start = m_dirEdit->text().isEmpty() ? QDir::homePath() : m_dirEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(this, "Select Target Directory", start);
    if (!dir.isEmpty())
        m_dirEdit->setText(dir);
}

void NewBoxDialog::addPort()
{
    const QString text = m_portEntry->text().trimmed();
    if (text.isEmpty())
        return;
    m_portList->addItem(text);
    m_portEntry->clear();
}

void NewBoxDialog::removeSelectedPort()
{
    const int row = m_portList->currentRow();
    if (row >= 0)
        delete m_portList->takeItem(row);
}

void NewBoxDialog::addDirMount()
{
    const QString text = m_dirEntry->text().trimmed();
    if (text.isEmpty())
        return;
    m_dirList->addItem(text);
    m_dirEntry->clear();
}

void NewBoxDialog::removeSelectedDirMount()
{
    const int row = m_dirList->currentRow();
    if (row >= 0)
        delete m_dirList->takeItem(row);
}

void NewBoxDialog::tryAccept()
{
    const QString dir = m_dirEdit->text().trimmed();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::warning(this, "New Box", "Target directory does not exist.");
        return;
    }
    accept();
}

QString NewBoxDialog::targetDir() const
{
    return QDir(m_dirEdit->text().trimmed()).absolutePath();
}

QString NewBoxDialog::conversationName() const
{
    return m_nameEdit->text().trimmed();
}

bool NewBoxDialog::yolo() const
{
    return m_yoloCheck->isChecked();
}

bool NewBoxDialog::rc() const
{
    return m_rcCheck->isChecked();
}

QStringList NewBoxDialog::ports() const
{
    QStringList result;
    for (int i = 0; i < m_portList->count(); ++i)
        result << m_portList->item(i)->text();
    return result;
}

QStringList NewBoxDialog::dirs() const
{
    QStringList result;
    for (int i = 0; i < m_dirList->count(); ++i)
        result << m_dirList->item(i)->text();
    return result;
}
