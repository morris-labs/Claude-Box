#include "NewBoxDialog.h"

#include "ConversationCatalog.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
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
// Per-item role holding a conversation's untagged title.
constexpr int kTitleRole = Qt::UserRole + 1;

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
    // Typing a path by hand should populate the picker too, not just Browse.
    connect(m_dirEdit, &QLineEdit::editingFinished, this, &NewBoxDialog::reloadConversations);

    m_sessionCombo = new QComboBox(this);
    // Conversation labels are long; without this the combo demands its
    // widest item and drags the whole dialog out past the screen. The
    // popup still sizes itself to the contents.
    m_sessionCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sessionCombo->setMinimumContentsLength(30);
    m_sessionCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    form->addRow("Conversation:", m_sessionCombo);
    connect(m_sessionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewBoxDialog::onConversationChanged);

    m_sessionHint = new QLabel(this);
    m_sessionHint->setWordWrap(true);
    m_sessionHint->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    form->addRow(QString(), m_sessionHint);

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
    reloadConversations();
}

void NewBoxDialog::browseForDir()
{
    const QString start = m_dirEdit->text().isEmpty() ? QDir::homePath() : m_dirEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(this, "Select Target Directory", start);
    if (!dir.isEmpty()) {
        m_dirEdit->setText(dir);
        reloadConversations();
    }
}

// Rebuilds the conversation picker for whatever directory is currently
// in the path field. Index 0 is always "new conversation" so the default
// behaviour is unchanged from before this picker existed.
void NewBoxDialog::reloadConversations()
{
    const QString dir = m_dirEdit->text().trimmed();
    if (dir == m_scannedDir)
        return;
    m_scannedDir = dir;

    const QList<ConversationInfo> conversations =
        dir.isEmpty() ? QList<ConversationInfo>() : ConversationCatalog::forDirectory(QDir(dir).absolutePath());

    QSignalBlocker blocker(m_sessionCombo);
    m_sessionCombo->clear();
    m_sessionCombo->addItem(QStringLiteral("Start a new conversation"), QString());

    for (const ConversationInfo &c : conversations) {
        QString label = c.title;
        if (!c.trackedBy.isEmpty())
            label += QStringLiteral("  [%1]").arg(c.trackedBy);
        label += QStringLiteral("  ·  %1  ·  %2 turns")
                     .arg(ConversationCatalog::relativeTime(c.lastActive))
                     .arg(c.userTurns);

        m_sessionCombo->addItem(label, c.sessionUuid);
        // The bare title, kept aside for the name auto-fill: the visible
        // label has the tracked-by tag and the age/turns tail glued on.
        m_sessionCombo->setItemData(m_sessionCombo->count() - 1, c.title, kTitleRole);

        QString tip = c.sessionUuid;
        if (!c.lastPrompt.isEmpty())
            tip += QStringLiteral("\n\nLast prompt: ") + c.lastPrompt;
        if (!c.trackedBy.isEmpty())
            tip += QStringLiteral("\n\nAlready tracked by box ") + c.trackedBy;
        m_sessionCombo->setItemData(m_sessionCombo->count() - 1, tip, Qt::ToolTipRole);
    }

    blocker.unblock();
    m_sessionCombo->setCurrentIndex(0);
    onConversationChanged(0);
}

void NewBoxDialog::onConversationChanged(int index)
{
    const QString uuid = m_sessionCombo->itemData(index).toString();

    if (uuid.isEmpty()) {
        const int existing = m_sessionCombo->count() - 1;
        m_sessionHint->setText(existing > 0
            ? QStringLiteral("A fresh session id is minted for this box. %1 existing conversation(s) "
                             "for this directory can be resumed instead.").arg(existing)
            : QStringLiteral("A fresh session id is minted for this box. "
                             "No existing conversations found for this directory."));
    } else {
        QString hint = QStringLiteral("Resumes %1 in the new box.").arg(uuid);
        const QString tip = m_sessionCombo->itemData(index, Qt::ToolTipRole).toString();
        if (tip.contains(QStringLiteral("Already tracked by box ")))
            hint += QStringLiteral(" This conversation already belongs to another box.");
        m_sessionHint->setText(hint);
    }

    // Give the box a meaningful name for free, without ever clobbering
    // something the user typed themselves.
    if (m_nameEdit && (m_nameEdit->text().isEmpty() || m_nameEdit->text() == m_autoFilledName)) {
        m_autoFilledName = uuid.isEmpty() ? QString() : m_sessionCombo->itemData(index, kTitleRole).toString();
        m_nameEdit->setText(m_autoFilledName);
    }
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

QString NewBoxDialog::sessionUuid() const
{
    return m_sessionCombo->currentData().toString();
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
