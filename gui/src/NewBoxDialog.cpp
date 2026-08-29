#include "NewBoxDialog.h"

#include "ConversationCatalog.h"
#include "ContainerPaths.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

// Per-item role holding a conversation's untagged title.
constexpr int kTitleRole = Qt::UserRole + 1;

// QSettings key holding the directories whose workspace-folder answer is
// remembered; a "no" is stored as the path with kNoWorkspaceMarker glued
// on, so the two answers live in one list without a nested group.
const char *kWorkspaceDirsKey = "newBox/workspaceDirs";
const QString kNoWorkspaceMarker = QStringLiteral("!");

// `claude --effort <level>` accepts exactly these, per `claude --help`.
const QStringList kEffortLevels = {"low", "medium", "high", "xhigh", "max"};

// `claude --model` takes either one of these aliases ("an alias for the
// latest model") or a full model name like claude-fable-5 -- which is why
// the combo is editable rather than a fixed list.
const QStringList kModelAliases = {"opus", "sonnet", "haiku", "fable"};

// Used only when the settings files name nothing at all, so that the
// combos always open on a real value rather than a placeholder.
const QString kFallbackModel = QStringLiteral("opus");
const QString kFallbackEffort = QStringLiteral("high");

// What `claude` itself would pick for `key`, read from the same files it
// reads and in the same precedence order (the project's local settings
// beat the project's, which beat the user's). Returns empty if none of
// them mention it.
QString settingsValue(const QString &targetDir, const QString &key)
{
    QStringList files;
    if (!targetDir.isEmpty()) {
        files << targetDir + "/.claude/settings.local.json"
              << targetDir + "/.claude/settings.json";
    }
    files << QDir::homePath() + "/.claude/settings.json";

    for (const QString &path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        const QString value = obj.value(key).toString();
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

// Selects `value` in a combo, adding it first if it isn't one of the
// known choices -- settings can name a pinned model like claude-opus-5,
// and dropping it on the floor would silently change which model runs.
void selectValue(QComboBox *combo, const QString &value)
{
    const int existing = combo->findData(value);
    if (existing >= 0) {
        combo->setCurrentIndex(existing);
        return;
    }
    combo->insertItem(0, value, value);
    combo->setCurrentIndex(0);
}

// Same rule as DockerBackend's slugifyIssueName (which in turn matches
// the old bash `tr -cs '[:alnum:]' '_'` pipeline). Duplicated rather than
// shared because this copy only ever feeds a preview label -- the folder
// that actually gets created is named by the backend's copy.
QString slugify(const QString &name)
{
    QString out;
    bool lastWasUnderscore = false;
    for (const QChar &c : name) {
        const bool alnum = c.unicode() < 128 && c.isLetterOrNumber();
        if (alnum) {
            out += c;
            lastWasUnderscore = false;
        } else if (!lastWasUnderscore) {
            out += '_';
            lastWasUnderscore = true;
        }
    }
    while (out.startsWith('_'))
        out.remove(0, 1);
    while (out.endsWith('_'))
        out.chop(1);
    return out;
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

    // Editable so a pinned full model name (claude-opus-5, a dated
    // snapshot, whatever a project standardizes on) can be typed in
    // instead of an alias.
    // Both combos open on the value the settings files already resolve to
    // (see reloadForDirectory) rather than on a "leave it to the settings"
    // placeholder: the point of the row is to show what this box will run
    // with, and a placeholder makes you go and look that up elsewhere.
    // The flag is then always passed explicitly.
    m_modelCombo = new QComboBox(this);
    m_modelCombo->setEditable(true);
    m_modelCombo->setInsertPolicy(QComboBox::NoInsert);
    for (const QString &alias : kModelAliases)
        m_modelCombo->addItem(alias, alias);
    form->addRow("Model:", m_modelCombo);

    m_effortCombo = new QComboBox(this);
    for (const QString &level : kEffortLevels)
        m_effortCombo->addItem(level, level);
    form->addRow("Effort:", m_effortCombo);

    connect(m_modelCombo, &QComboBox::currentTextChanged, this, [this] { m_modelTouched = true; });
    connect(m_effortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { m_effortTouched = true; });

    // The old bash launcher did this only for the one project that ships
    // a new-issue.sh; it works anywhere, so it's a checkbox. Ticked
    // automatically for a directory that has a provisioner, which is what
    // that project's boxes did implicitly before.
    m_workspaceCheck = new QCheckBox("Give the agent its own subfolder, named after the conversation", this);
    form->addRow(QString(), m_workspaceCheck);
    m_workspaceHint = new QLabel(this);
    m_workspaceHint->setWordWrap(true);
    m_workspaceHint->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    form->addRow(QString(), m_workspaceHint);
    connect(m_workspaceCheck, &QCheckBox::toggled, this, &NewBoxDialog::updateWorkspaceHint);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &NewBoxDialog::updateWorkspaceHint);

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

// Rebuilds the conversation picker for whatever directory is currently
// in the path field. Index 0 is always "new conversation" so the default
// behaviour is unchanged from before the picker existed.
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

    // Settings are per-directory too (a project can pin its own model), so
    // the two selections are re-resolved whenever the target changes --
    // but only while the user hasn't overridden them, since re-resolving
    // over a deliberate choice would quietly undo it.
    // Signals blocked while doing it: selecting a value here is this
    // dialog resolving settings, not the user choosing, and letting it
    // through would immediately mark the combo as overridden and freeze
    // it on the first directory ever shown.
    if (!m_modelTouched) {
        const QSignalBlocker block(m_modelCombo);
        const QString model = settingsValue(absDir, QStringLiteral("model"));
        selectValue(m_modelCombo, model.isEmpty() ? kFallbackModel : model);
    }
    if (!m_effortTouched) {
        const QSignalBlocker block(m_effortCombo);
        const QString effort = settingsValue(absDir, QStringLiteral("effortLevel"));
        selectValue(m_effortCombo, effort.isEmpty() ? kFallbackEffort : effort);
    }

    // Whether to give the agent its own folder is a property of the tree,
    // not of the moment, so the choice is remembered per directory. A tree
    // that still carries a new-issue.sh from the bash era seeds that as
    // "yes" the first time it's seen -- the script isn't run any more (the
    // agent does its own setup now), but its presence is a reliable marker
    // that this tree is organised into per-issue folders.
    m_hasProvisioner = !absDir.isEmpty()
        && QFileInfo(absDir + "/new-issue.sh").isExecutable();
    if (!absDir.isEmpty()) {
        const QStringList remembered = QSettings().value(kWorkspaceDirsKey).toStringList();
        m_workspaceCheck->setChecked(remembered.contains(absDir)
                                     || (m_hasProvisioner && !remembered.contains(kNoWorkspaceMarker + absDir)));
    }
    updateWorkspaceHint();
}

// Says, in the form, exactly what will appear on disk -- the slug rules
// are the container's (alphanumeric runs joined by underscores), so
// showing the result beats explaining it.
void NewBoxDialog::updateWorkspaceHint()
{
    if (!m_workspaceCheck->isChecked()) {
        m_workspaceHint->setText(QStringLiteral("The agent works in the target directory itself."));
        return;
    }

    const QString slug = slugify(m_nameEdit->text().trimmed());
    if (slug.isEmpty()) {
        m_workspaceHint->setText(QStringLiteral("Needs a conversation name -- the folder is named after it."));
        return;
    }

    const QString dir = m_dirEdit->text().trimmed();
    const QString path = dir + "/" + slug;
    if (QDir(path).exists())
        m_workspaceHint->setText(QStringLiteral("%1/ already exists -- it will be reused as-is.").arg(slug));
    else
        m_workspaceHint->setText(QStringLiteral("%1/ will be created, and the agent told to set it up "
                                               "per this directory's CLAUDE.md and work there.").arg(slug));
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
    // keeps file references copied out of the box meaningful -- "mirror"
    // meaning the mapped container path, since on Windows the literal
    // host string can't be a container path at all (see ContainerPaths.h).
    const QString container = m_containerDirEdit->text().trimmed().isEmpty()
        ? ContainerPaths::hostToContainer(host) : m_containerDirEdit->text().trimmed();

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

    if (m_workspaceCheck->isChecked() && slugify(m_nameEdit->text().trimmed()).isEmpty()) {
        QMessageBox::warning(this, "New Box",
                             "A workspace folder is named after the conversation, so give this "
                             "one a name (with at least one letter or digit) -- or untick the box "
                             "to work in the target directory itself.");
        return;
    }
    rememberWorkspaceChoice();
    accept();
}

// Records this directory's answer so the next box for the same tree opens
// with the box already in the right state. Both answers are stored: an
// explicit "no" has to outrank the new-issue.sh seeding above, or a tree
// that still has that script could never be unticked for good.
void NewBoxDialog::rememberWorkspaceChoice()
{
    const QString dir = targetDir();
    if (dir.isEmpty())
        return;

    QSettings settings;
    QStringList remembered = settings.value(kWorkspaceDirsKey).toStringList();
    remembered.removeAll(dir);
    remembered.removeAll(kNoWorkspaceMarker + dir);
    remembered << (m_workspaceCheck->isChecked() ? dir : (kNoWorkspaceMarker + dir));
    settings.setValue(kWorkspaceDirsKey, remembered);
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

bool NewBoxDialog::workspaceSubdir() const
{
    return m_workspaceCheck->isChecked();
}

QString NewBoxDialog::model() const
{
    // currentText, not currentData: the combo is editable, so a full model
    // name typed straight into it has no item behind it.
    return m_modelCombo->currentText().trimmed();
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
