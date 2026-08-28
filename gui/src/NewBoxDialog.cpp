#include "NewBoxDialog.h"

#include "ConversationCatalog.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

// Per-item role holding a conversation's untagged title.
constexpr int kTitleRole = Qt::UserRole + 1;

// Shown for agent/effort when nothing should be passed on the command
// line at all, leaving ~/.claude/settings.json in charge.
const char *kDefaultChoice = "Default (from settings)";

// `claude --effort <level>` accepts exactly these, per `claude --help`.
const QStringList kEffortLevels = {"low", "medium", "high", "xhigh", "max"};

// Agent definitions live in <dir>/.claude/agents/*.md, both per-project
// and in the home directory; the frontmatter `name:` is the identifier
// `--agent` wants, falling back to the filename for files without one.
// This is only used to populate a combo the user can also type into, so
// an agent this misses is still reachable.
QStringList discoverAgents(const QString &targetDir)
{
    QStringList result;

    QStringList roots;
    if (!targetDir.isEmpty())
        roots << targetDir + "/.claude/agents";
    roots << QDir::homePath() + "/.claude/agents";

    for (const QString &root : roots) {
        const QFileInfoList files = QDir(root).entryInfoList({"*.md"}, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            QString name = fi.completeBaseName();

            QFile f(fi.absoluteFilePath());
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                // Frontmatter only: stop at the closing --- so a `name:`
                // deeper in the prose can't be mistaken for the header.
                bool inFrontmatter = false;
                while (!f.atEnd()) {
                    const QString line = QString::fromUtf8(f.readLine()).trimmed();
                    if (line == "---") {
                        if (inFrontmatter)
                            break;
                        inFrontmatter = true;
                        continue;
                    }
                    if (inFrontmatter && line.startsWith("name:")) {
                        const QString value = line.mid(5).trimmed();
                        if (!value.isEmpty())
                            name = value;
                        break;
                    }
                }
            }

            if (!name.isEmpty() && !result.contains(name))
                result << name;
        }
    }

    return result;
}

// The "list of things you can add to and remove from" frame shared by
// the port and mount groups. The caller fills `entryRow` with whatever
// fields that particular group needs.
QListWidget *makeListGroup(QVBoxLayout *parent, const QString &title, QLayout *entryRow,
                           QObject *receiver, const char *addSlot, const char *removeSlot)
{
    auto *group = new QGroupBox(title);
    auto *layout = new QVBoxLayout(group);

    auto *list = new QListWidget(group);
    list->setMaximumHeight(90);
    layout->addWidget(list);
    layout->addLayout(entryRow);

    auto *buttons = new QHBoxLayout();
    auto *addButton = new QPushButton("Add", group);
    auto *removeButton = new QPushButton("Remove Selected", group);
    buttons->addStretch();
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton);
    layout->addLayout(buttons);

    QObject::connect(addButton, SIGNAL(clicked()), receiver, addSlot);
    QObject::connect(removeButton, SIGNAL(clicked()), receiver, removeSlot);

    parent->addWidget(group);
    return list;
}

// Every list item carries the docker-syntax value in Qt::UserRole and
// shows a friendlier "A -> B" label; this is how both get read back.
bool listContainsValue(QListWidget *list, const QString &value)
{
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(Qt::UserRole).toString() == value)
            return true;
    }
    return false;
}

void addListValue(QListWidget *list, const QString &label, const QString &value)
{
    auto *item = new QListWidgetItem(label, list);
    item->setData(Qt::UserRole, value);
}

QStringList listValues(const QListWidget *list)
{
    QStringList result;
    for (int i = 0; i < list->count(); ++i)
        result << list->item(i)->data(Qt::UserRole).toString();
    return result;
}

} // namespace

NewBoxDialog::NewBoxDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("New Box");
    resize(560, 620);

    auto *mainLayout = new QVBoxLayout(this);

    auto *form = new QFormLayout();

    auto *dirRow = new QHBoxLayout();
    m_dirEdit = new QLineEdit(this);
    auto *browseButton = new QPushButton("Browse…", this);
    dirRow->addWidget(m_dirEdit);
    dirRow->addWidget(browseButton);
    form->addRow("Target directory:", dirRow);
    connect(browseButton, &QPushButton::clicked, this, &NewBoxDialog::browseForDir);
    // Typing a path by hand should populate the pickers too, not just Browse.
    connect(m_dirEdit, &QLineEdit::editingFinished, this, &NewBoxDialog::reloadForDirectory);

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

    // Editable: agent definitions can come from plugins and other places
    // this dialog doesn't scan, and typing one in has to stay possible.
    m_agentCombo = new QComboBox(this);
    m_agentCombo->setEditable(true);
    m_agentCombo->setInsertPolicy(QComboBox::NoInsert);
    form->addRow("Agent:", m_agentCombo);

    m_effortCombo = new QComboBox(this);
    m_effortCombo->addItem(kDefaultChoice, QString());
    for (const QString &level : kEffortLevels)
        m_effortCombo->addItem(level, level);
    form->addRow("Effort:", m_effortCombo);

    // On by default: these boxes are the sandbox the flag asks for, and
    // stopping at every permission prompt is the whole thing they exist
    // to avoid.
    m_skipPermsCheck = new QCheckBox("Bypass permission prompts (--dangerously-skip-permissions)", this);
    m_skipPermsCheck->setChecked(true);
    form->addRow(QString(), m_skipPermsCheck);

    mainLayout->addLayout(form);

    // --- ports: host field + container field, joined into HOST:CONTAINER
    auto *portRow = new QGridLayout();
    m_hostPortEdit = new QLineEdit(this);
    m_hostPortEdit->setValidator(new QIntValidator(1, 65535, this));
    m_hostPortEdit->setPlaceholderText("8080");
    m_containerPortEdit = new QLineEdit(this);
    m_containerPortEdit->setValidator(new QIntValidator(1, 65535, this));
    m_containerPortEdit->setPlaceholderText("same as host");
    portRow->addWidget(new QLabel("On the host:", this), 0, 0);
    portRow->addWidget(m_hostPortEdit, 0, 1);
    portRow->addWidget(new QLabel("In the box:", this), 0, 2);
    portRow->addWidget(m_containerPortEdit, 0, 3);
    m_portList = makeListGroup(mainLayout, "Port mappings", portRow,
                               this, SLOT(addPort()), SLOT(removeSelectedPort()));
    connect(m_hostPortEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addPort);
    connect(m_containerPortEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addPort);

    // --- mounts: host path (with its own Browse) + where it lands inside
    auto *dirMountRow = new QGridLayout();
    m_hostDirEdit = new QLineEdit(this);
    m_hostDirEdit->setPlaceholderText("/path/on/host");
    auto *mountBrowse = new QPushButton("Browse…", this);
    m_containerDirEdit = new QLineEdit(this);
    m_containerDirEdit->setPlaceholderText("same path inside the box");
    dirMountRow->addWidget(new QLabel("On the host:", this), 0, 0);
    dirMountRow->addWidget(m_hostDirEdit, 0, 1);
    dirMountRow->addWidget(mountBrowse, 0, 2);
    dirMountRow->addWidget(new QLabel("In the box:", this), 1, 0);
    dirMountRow->addWidget(m_containerDirEdit, 1, 1, 1, 2);
    m_dirList = makeListGroup(mainLayout, "Extra dir mounts", dirMountRow,
                              this, SLOT(addDirMount()), SLOT(removeSelectedDirMount()));
    connect(mountBrowse, &QPushButton::clicked, this, &NewBoxDialog::browseForMountDir);
    connect(m_hostDirEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addDirMount);
    connect(m_containerDirEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addDirMount);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewBoxDialog::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &NewBoxDialog::reject);
    mainLayout->addWidget(buttons);

    reloadForDirectory();
}

void NewBoxDialog::setInitialDir(const QString &dir)
{
    m_dirEdit->setText(dir);
    reloadForDirectory();
}

void NewBoxDialog::browseForDir()
{
    const QString start = m_dirEdit->text().isEmpty() ? QDir::homePath() : m_dirEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(this, "Select Target Directory", start);
    if (!dir.isEmpty()) {
        m_dirEdit->setText(dir);
        reloadForDirectory();
    }
}

void NewBoxDialog::browseForMountDir()
{
    const QString start = m_hostDirEdit->text().isEmpty() ? QDir::homePath() : m_hostDirEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(this, "Select Directory to Mount", start);
    if (!dir.isEmpty())
        m_hostDirEdit->setText(dir);
}

// Rebuilds the conversation and agent pickers for whatever directory is
// currently in the path field. Conversation index 0 is always "new
// conversation" so the default behaviour is unchanged from before the
// picker existed.
void NewBoxDialog::reloadForDirectory()
{
    const QString dir = m_dirEdit->text().trimmed();
    if (dir == m_scannedDir && m_sessionCombo->count() > 0)
        return;
    m_scannedDir = dir;
    const QString absDir = dir.isEmpty() ? QString() : QDir(dir).absolutePath();

    const QList<ConversationInfo> conversations =
        absDir.isEmpty() ? QList<ConversationInfo>() : ConversationCatalog::forDirectory(absDir);

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

    // Agents are per-directory too (a project can define its own), so
    // they get rebuilt here as well -- preserving whatever was typed or
    // chosen, since it may well name an agent from somewhere this scan
    // doesn't look.
    const QString previousAgent = m_agentCombo->currentIndex() == 0 ? QString() : m_agentCombo->currentText().trimmed();
    QSignalBlocker agentBlocker(m_agentCombo);
    m_agentCombo->clear();
    m_agentCombo->addItem(kDefaultChoice, QString());
    for (const QString &agent : discoverAgents(absDir))
        m_agentCombo->addItem(agent, agent);
    if (previousAgent.isEmpty()) {
        m_agentCombo->setCurrentIndex(0);
    } else {
        const int existing = m_agentCombo->findText(previousAgent);
        if (existing >= 0)
            m_agentCombo->setCurrentIndex(existing);
        else
            m_agentCombo->setCurrentText(previousAgent);
    }
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
    const QString host = m_hostPortEdit->text().trimmed();
    // Both sides are usually the same number, so an empty container field
    // mirrors the host one rather than being an error.
    const QString container = m_containerPortEdit->text().trimmed().isEmpty()
        ? host : m_containerPortEdit->text().trimmed();

    if (host.isEmpty()) {
        QMessageBox::warning(this, "Port mapping", "Enter the host port to publish.");
        return;
    }

    const QString value = host + ":" + container;
    if (!listContainsValue(m_portList, value))
        addListValue(m_portList, QStringLiteral("host %1  →  box %2").arg(host, container), value);

    m_hostPortEdit->clear();
    m_containerPortEdit->clear();
    m_hostPortEdit->setFocus();
}

void NewBoxDialog::removeSelectedPort()
{
    const int row = m_portList->currentRow();
    if (row >= 0)
        delete m_portList->takeItem(row);
}

void NewBoxDialog::addDirMount()
{
    const QString hostRaw = m_hostDirEdit->text().trimmed();
    if (hostRaw.isEmpty()) {
        QMessageBox::warning(this, "Dir mount", "Enter the host directory to mount.");
        return;
    }

    const QString host = QFileInfo(hostRaw).absoluteFilePath();
    if (!QDir(host).exists()) {
        // Docker would happily create a root-owned directory here, which
        // then isn't writable by the box's `user` user -- easier to
        // catch it now than to debug it inside the container later.
        QMessageBox::warning(this, "Dir mount", host + " does not exist.");
        return;
    }

    // Mirroring the host path is both the common case and the one that
    // keeps file references copied out of the box meaningful.
    const QString container = m_containerDirEdit->text().trimmed().isEmpty()
        ? host : m_containerDirEdit->text().trimmed();

    const QString value = host + ":" + container;
    if (!listContainsValue(m_dirList, value))
        addListValue(m_dirList, QStringLiteral("%1  →  %2").arg(host, container), value);

    m_hostDirEdit->clear();
    m_containerDirEdit->clear();
    m_hostDirEdit->setFocus();
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

bool NewBoxDialog::skipPermissions() const
{
    return m_skipPermsCheck->isChecked();
}

QString NewBoxDialog::agent() const
{
    // The combo is editable, so the placeholder label can end up as the
    // literal text; either way it means "pass no --agent at all".
    const QString text = m_agentCombo->currentText().trimmed();
    return text == QLatin1String(kDefaultChoice) ? QString() : text;
}

QString NewBoxDialog::effort() const
{
    return m_effortCombo->currentData().toString();
}

QStringList NewBoxDialog::ports() const
{
    return listValues(m_portList);
}

QStringList NewBoxDialog::dirs() const
{
    return listValues(m_dirList);
}
