#include "NewBoxDialog.h"

#include "BoxRecord.h"
#include "CollapsibleSection.h"
#include "ConversationCatalog.h"
#include "ContainerPaths.h"
#include "ManageSshRemotesDialog.h"
#include "SshRemoteCatalog.h"
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
#include <QFrame>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
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

// `claude --model` accepts short aliases ("an alias for the latest model")
// or full model IDs. The combo is editable so the user can type any value.
const QStringList kModelAliases = {"opus", "sonnet", "haiku", "fable"};

// Full model IDs, newest first. The empty string inserts a visual separator
// in the combo between the aliases above and these pinned IDs.
const QStringList kModelIds = {
    QString(),              // separator
    QStringLiteral("claude-fable-5"),
    QStringLiteral("claude-opus-5"),
    QStringLiteral("claude-sonnet-5"),
    QStringLiteral("claude-opus-4-8"),
    QStringLiteral("claude-opus-4-7"),
    QStringLiteral("claude-opus-4-6"),
    QStringLiteral("claude-sonnet-4-6"),
    QStringLiteral("claude-haiku-4-5"),
};

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

// The "list of things you can add to and remove from" content shared by
// the port and mount sections, installed into `section`'s collapsible
// body. The caller fills `entryRow` with whatever fields that particular
// section needs.
QListWidget *makeListGroup(CollapsibleSection *section, QLayout *entryRow,
                           QObject *receiver, const char *addSlot, const char *removeSlot)
{
    auto *layout = new QVBoxLayout();

    auto *list = new QListWidget();
    list->setMaximumHeight(90);
    layout->addWidget(list);
    layout->addLayout(entryRow);

    auto *buttons = new QHBoxLayout();
    auto *addButton = new QPushButton("Add");
    auto *removeButton = new QPushButton("Remove Selected");
    buttons->addStretch();
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton);
    layout->addLayout(buttons);

    QObject::connect(addButton, SIGNAL(clicked()), receiver, addSlot);
    QObject::connect(removeButton, SIGNAL(clicked()), receiver, removeSlot);

    section->setContentLayout(layout);
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

// Label for one SshRemoteCatalog entry's checkbox in the Remote Port
// Forwarding section.
QString remoteLabel(const SshRemote &r)
{
    return QStringLiteral("%1  —  %2  (%3 forward%4)")
        .arg(r.name, r.host.isEmpty() ? QStringLiteral("(no target set)") : r.host)
        .arg(r.forwards.size())
        .arg(r.forwards.size() == 1 ? QString() : QStringLiteral("s"));
}

} // namespace

NewBoxDialog::NewBoxDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("New Box");
    resize(640, 640);

    // Content lives in a scroll area rather than directly in the dialog:
    // the SSH-forwards section pushed this well past a size that fits on
    // a real screen, and a dialog whose *minimum* height is dictated by
    // however much content it happens to hold (rather than by the fixed
    // size above) is a WM/multi-monitor placement bug waiting to happen
    // every time a field gets added -- it already was one. Scrolling
    // keeps the dialog's own footprint exactly what resize() asked for,
    // no matter how much ends up inside it.
    //
    // Horizontal scrolling is deliberately disabled, not just left
    // unused: setWidgetResizable(true) alone doesn't stop a wrapping
    // QLabel from reporting a wide sizeHint before it's actually been
    // laid out at the viewport's width, and a QScrollArea left free to
    // grow horizontally to satisfy that shows a sideways scrollbar on
    // first open instead of just wrapping the text. Turning it off forces
    // the content to actually fit (and wrap) at the viewport's width.
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outerLayout->addWidget(scrollArea, 1);

    auto *scrollContent = new QWidget(scrollArea);
    scrollArea->setWidget(scrollContent);

    auto *mainLayout = new QVBoxLayout(scrollContent);

    auto *form = new QFormLayout();

    auto *dirRow = new QHBoxLayout();
    m_dirEdit = new QLineEdit(this);
    m_dirBrowseButton = new QPushButton("Browse…", this);
    dirRow->addWidget(m_dirEdit);
    dirRow->addWidget(m_dirBrowseButton);
    form->addRow("Target directory:", dirRow);
    connect(m_dirBrowseButton, &QPushButton::clicked, this, &NewBoxDialog::browseForDir);
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
    for (const QString &id : kModelIds) {
        if (id.isEmpty()) {
            // Visual separator between aliases and full model IDs.
            m_modelCombo->insertSeparator(m_modelCombo->count());
        } else {
            m_modelCombo->addItem(id, id);
        }
    }
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

    // Three collapsed-by-default sections, in the order asked for: Local
    // (docker -p), Remote (any number of SSH tunnels), then folders. All
    // three used to be shown expanded, which is exactly what pushed this
    // dialog's minimum height past what fits on a real screen (see the
    // "Fix New/Edit Box dialog getting shoved onto another monitor"
    // history) -- collapsed keeps the common case (none of this needed)
    // compact, and loadForEdit() expands whichever ones a box already has
    // configured so existing settings are never hidden away.

    // --- Local Port Forwarding: host field + container field, joined
    // into HOST:CONTAINER (docker -p).
    m_portsSection = new CollapsibleSection("Local Port Forwarding", this);
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
    m_portList = makeListGroup(m_portsSection, portRow, this, SLOT(addPort()), SLOT(removeSelectedPort()));
    connect(m_hostPortEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addPort);
    connect(m_containerPortEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addPort);
    mainLayout->addWidget(m_portsSection);

    // --- Remote Port Forwarding: which of the host-wide SshRemoteCatalog
    // entries this box attaches to (one checkbox each -- what a remote
    // actually connects to is defined once in the catalog and shared by
    // every box that attaches to it). "Manage Remotes…" opens the catalog
    // editor without leaving this dialog.
    m_remotesSection = new CollapsibleSection("Remote Port Forwarding", this);
    auto *remoteLayout = new QVBoxLayout();
    m_remoteChecks = new QWidget();
    m_remoteChecksLayout = new QVBoxLayout(m_remoteChecks);
    m_remoteChecksLayout->setContentsMargins(0, 0, 0, 0);
    m_remoteChecksLayout->setSpacing(3);
    remoteLayout->addWidget(m_remoteChecks);
    // Only ever shown by loadForEdit() -- a legacy per-box remote (see
    // BoxRecord::sshRemotes) has no name and nothing here to attach or
    // detach it from; it round-trips untouched as long as this dialog
    // never overwrites the field, which it doesn't.
    m_legacyRemotesHint = new QLabel(this);
    m_legacyRemotesHint->setWordWrap(true);
    m_legacyRemotesHint->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    m_legacyRemotesHint->setVisible(false);
    remoteLayout->addWidget(m_legacyRemotesHint);
    auto *remoteButtons = new QHBoxLayout();
    auto *manageRemotesButton = new QPushButton("Manage Remotes…");
    remoteButtons->addStretch();
    remoteButtons->addWidget(manageRemotesButton);
    remoteLayout->addLayout(remoteButtons);
    connect(manageRemotesButton, &QPushButton::clicked, this, &NewBoxDialog::openManageRemotes);
    m_remotesSection->setContentLayout(remoteLayout);
    mainLayout->addWidget(m_remotesSection);

    // --- Add folders to sandbox: host path (with its own Browse) + where
    // it lands inside (extra -v mounts, beyond the target directory
    // itself).
    m_dirsSection = new CollapsibleSection("Add folders to sandbox", this);
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
    m_dirList = makeListGroup(m_dirsSection, dirMountRow, this, SLOT(addDirMount()), SLOT(removeSelectedDirMount()));
    connect(mountBrowse, &QPushButton::clicked, this, &NewBoxDialog::browseForMountDir);
    connect(m_hostDirEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addDirMount);
    connect(m_containerDirEdit, &QLineEdit::returnPressed, this, &NewBoxDialog::addDirMount);
    mainLayout->addWidget(m_dirsSection);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &NewBoxDialog::tryAccept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &NewBoxDialog::reject);
    // Outside the scroll area, not appended to mainLayout -- Ok/Cancel
    // should always be visible, not something you have to scroll down to.
    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(9, 6, 9, 9);
    buttonRow->addWidget(m_buttons);
    outerLayout->addLayout(buttonRow);

    reloadForDirectory();
    refreshRemoteList(); // populate the checklist from the catalog even for a brand-new box
}

void NewBoxDialog::loadForEdit(const BoxRecord &rec)
{
    m_editMode = true;
    setWindowTitle("Edit Box: " + rec.name);
    if (auto *ok = m_buttons->button(QDialogButtonBox::Ok))
        ok->setText("Save Changes");

    // Neither is safe to change after creation: the mount and the
    // ~/.claude/projects encoding are keyed off the directory, and
    // switching which conversation a box resumes is what adopting a
    // conversation into a *new* box is for, not this.
    m_dirEdit->setText(rec.targetDir);
    m_dirEdit->setEnabled(false);
    m_dirBrowseButton->setEnabled(false);

    reloadForDirectory(); // populates model/effort defaults and the (disabled) conversation combo
    m_sessionCombo->setEnabled(false);
    m_sessionHint->setText(rec.sessionUuid.isEmpty() ? QStringLiteral("No session recorded for this box.")
        : QStringLiteral("Resumes %1. Editing a box doesn't change which conversation it resumes.").arg(rec.sessionUuid));

    m_nameEdit->setText(rec.conversationName);
    m_autoFilledName = rec.conversationName;

    selectValue(m_modelCombo, rec.model.isEmpty() ? kFallbackModel : rec.model);
    m_modelTouched = true;
    selectValue(m_effortCombo, rec.effort.isEmpty() ? kFallbackEffort : rec.effort);
    m_effortTouched = true;

    // The workspace folder (if any) was chosen at creation time and the
    // agent may already be relying on it; shown for reference, not editable.
    m_workspaceCheck->setChecked(!rec.workspaceDir.isEmpty());
    m_workspaceCheck->setEnabled(false);
    if (!rec.workspaceDir.isEmpty())
        m_workspaceHint->setText(rec.workspaceDir + "/ -- set when this box was created; not editable here.");

    m_skipPermsCheck->setChecked(rec.skipPermissions);

    seedExtras(rec);
}

// Forks `source`'s conversation into a brand-new, independent box: same
// directory (locked -- the fork needs source's own transcript, which is
// rooted there), everything else copied over as an editable starting
// point. The new box always mints its own session id (see
// DockerBackend::createNew's forkFromUuid) rather than resuming
// `source`'s, so unlike loadForEdit() the conversation combo isn't tied
// to `source` at all -- it's left on "Start a new conversation" and just
// disabled, to make clear that choice isn't live in this mode.
void NewBoxDialog::loadForFork(const BoxRecord &source)
{
    m_forkMode = true;
    m_forkSourceUuid = source.sessionUuid;
    setWindowTitle("Fork Conversation: " + source.conversationName);
    if (auto *ok = m_buttons->button(QDialogButtonBox::Ok))
        ok->setText("Create Fork");

    m_dirEdit->setText(source.targetDir);
    m_dirEdit->setEnabled(false);
    m_dirBrowseButton->setEnabled(false);

    reloadForDirectory(); // populates model/effort defaults and the conversation combo
    m_sessionCombo->setEnabled(false);
    m_sessionHint->setText(QStringLiteral(
        "Starts a new, independent conversation preloaded with %1's history up to now. "
        "Later turns in either conversation don't affect the other.").arg(source.conversationName));

    m_nameEdit->setText(source.conversationName + QStringLiteral(" (fork)"));
    m_autoFilledName = m_nameEdit->text();

    selectValue(m_modelCombo, source.model.isEmpty() ? kFallbackModel : source.model);
    m_modelTouched = true;
    selectValue(m_effortCombo, source.effort.isEmpty() ? kFallbackEffort : source.effort);
    m_effortTouched = true;

    // Carried over from source as a starting point, not locked -- see the
    // class comment on why a fork stays closer to New Box than to Edit.
    m_workspaceCheck->setChecked(!source.workspaceDir.isEmpty());
    m_skipPermsCheck->setChecked(source.skipPermissions);

    seedExtras(source);
}

// Ports, mounts, and SSH remote attachments -- shared by loadForEdit() and
// loadForFork(), both of which seed the form from an existing record's
// fields.
void NewBoxDialog::seedExtras(const BoxRecord &rec)
{
    for (const QString &p : rec.ports) {
        const int colon = p.indexOf(':');
        const QString host = colon >= 0 ? p.left(colon) : p;
        const QString container = colon >= 0 ? p.mid(colon + 1) : p;
        addListValue(m_portList, QStringLiteral("host %1  →  box %2").arg(host, container), p);
    }
    for (const QString &d : rec.dirs) {
        QString hostRaw, containerPath;
        ContainerPaths::splitMountSpec(d, hostRaw, containerPath);
        addListValue(m_dirList, QStringLiteral("%1  →  %2").arg(hostRaw, containerPath), d);
    }

    m_checkedRemoteNames = rec.sshRemoteRefs;
    refreshRemoteList();
    if (!rec.sshRemotes.isEmpty()) {
        m_legacyRemotesHint->setText(QStringLiteral(
            "%1 legacy remote%2 configured directly on this box (from before shared remotes existed): "
            "kept as-is, not shown or editable here.")
                .arg(rec.sshRemotes.size())
                .arg(rec.sshRemotes.size() == 1 ? QString() : QStringLiteral("s")));
        m_legacyRemotesHint->setVisible(true);
    }

    // A section with something already in it is expanded on load, so an
    // existing box's configuration is never hidden a click away.
    m_portsSection->setExpanded(!rec.ports.isEmpty());
    m_remotesSection->setExpanded(!rec.sshRemoteRefs.isEmpty() || !rec.sshRemotes.isEmpty());
    m_dirsSection->setExpanded(!rec.dirs.isEmpty());
}

void NewBoxDialog::preallocatePorts(const QList<int> &ports)
{
    m_portList->clear();
    for (int port : ports) {
        const QString p = QStringLiteral("%1:%1").arg(port);
        addListValue(m_portList, QStringLiteral("host %1  →  box %1").arg(port), p);
    }
    m_portsSection->setExpanded(!ports.isEmpty());
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
    m_folderMatchDir.clear();
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
// showing the result beats explaining it. Also detects an existing
// matching subfolder and pre-populates the conversation combo with its
// conversations (folder-match), restoring the parent dir's list when the
// match is no longer applicable.
void NewBoxDialog::updateWorkspaceHint()
{
    auto restoreParentConversations = [this] {
        if (!m_folderMatchDir.isEmpty()) {
            m_folderMatchDir.clear();
            m_scannedDir.clear(); // force reloadForDirectory to rescan
            reloadForDirectory(); // also calls updateWorkspaceHint; m_folderMatchDir is now clear so no loop
        }
    };

    if (!m_workspaceCheck->isChecked()) {
        m_workspaceHint->setText(QStringLiteral("The agent works in the target directory itself."));
        restoreParentConversations();
        return;
    }

    const QString slug = slugify(m_nameEdit->text().trimmed());
    if (slug.isEmpty()) {
        m_workspaceHint->setText(QStringLiteral("Needs a conversation name -- the folder is named after it."));
        restoreParentConversations();
        return;
    }

    const QString dir = m_dirEdit->text().trimmed();
    const QString path = dir + "/" + slug;

    if (!QDir(path).exists()) {
        restoreParentConversations();
        m_workspaceHint->setText(QStringLiteral("%1/ will be created, and the agent told to set it up "
                                               "per this directory's CLAUDE.md and work there.").arg(slug));
        return;
    }

    // Folder exists. If this is a newly matched folder, rebuild the combo
    // from the subfolder's conversations.
    if (m_folderMatchDir != path) {
        m_folderMatchDir = path;
        const QString absPath = QDir(path).absolutePath();
        const QList<ConversationInfo> convs = ConversationCatalog::forDirectory(absPath);

        {
            QSignalBlocker blocker(m_sessionCombo);
            m_sessionCombo->clear();
            m_sessionCombo->addItem(QStringLiteral("Start a new conversation"), QString());
            for (const ConversationInfo &c : convs) {
                QString label = c.title;
                if (!c.trackedBy.isEmpty())
                    label += QStringLiteral("  [%1]").arg(c.trackedBy);
                label += QStringLiteral("  ·  %1  ·  %2 turns")
                             .arg(ConversationCatalog::relativeTime(c.lastActive))
                             .arg(c.userTurns);
                m_sessionCombo->addItem(label, c.sessionUuid);
                m_sessionCombo->setItemData(m_sessionCombo->count() - 1, c.title, kTitleRole);
                QString tip = c.sessionUuid;
                if (!c.lastPrompt.isEmpty())
                    tip += QStringLiteral("\n\nLast prompt: ") + c.lastPrompt;
                if (!c.trackedBy.isEmpty())
                    tip += QStringLiteral("\n\nAlready tracked by box ") + c.trackedBy;
                m_sessionCombo->setItemData(m_sessionCombo->count() - 1, tip, Qt::ToolTipRole);
            }
        }

        if (!convs.isEmpty()) {
            m_sessionCombo->setCurrentIndex(1);
            onConversationChanged(1);
            m_workspaceHint->setText(QStringLiteral(
                "%1/ already exists -- %2 conversation(s) found. Selecting the most recent.")
                .arg(slug).arg(convs.size()));
        } else {
            m_sessionCombo->setCurrentIndex(0);
            onConversationChanged(0);
            m_workspaceHint->setText(
                QStringLiteral("%1/ already exists -- it will be reused as-is.").arg(slug));
        }
        return;
    }

    // Already showing this folder's conversations -- just refresh the hint.
    const int nConvs = m_sessionCombo->count() - 1; // subtract "Start a new conversation"
    if (nConvs > 0)
        m_workspaceHint->setText(QStringLiteral(
            "%1/ already exists -- %2 conversation(s) found. Selecting the most recent.")
            .arg(slug).arg(nConvs));
    else
        m_workspaceHint->setText(
            QStringLiteral("%1/ already exists -- it will be reused as-is.").arg(slug));
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
        // then isn't writable by the container user -- easier to catch it
        // now than to debug it inside the container later.
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

// Folds whatever the user has currently ticked back into
// m_checkedRemoteNames. Called only before the checkbox list is about to be
// rebuilt from a possibly-changed catalog (see openManageRemotes()) -- NOT
// from the seeding path in loadForEdit(), where the boxes still hold the
// ctor's all-unchecked state and folding them in would wipe the seed.
void NewBoxDialog::syncCheckedRemoteNames()
{
    for (QCheckBox *box : m_remoteChecks->findChildren<QCheckBox *>()) {
        const QString name = box->property("remoteName").toString();
        if (name.isEmpty())
            continue;
        if (box->isChecked()) {
            if (!m_checkedRemoteNames.contains(name))
                m_checkedRemoteNames << name;
        } else {
            m_checkedRemoteNames.removeAll(name);
        }
    }
}

void NewBoxDialog::refreshRemoteList()
{
    while (QLayoutItem *item = m_remoteChecksLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const QList<SshRemote> remotes = SshRemoteCatalog::loadAll();
    if (remotes.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("No remotes defined yet — use “Manage Remotes…”."),
                                 m_remoteChecks);
        empty->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
        m_remoteChecksLayout->addWidget(empty);
        return;
    }

    for (const SshRemote &r : remotes) {
        auto *box = new QCheckBox(remoteLabel(r), m_remoteChecks);
        box->setProperty("remoteName", r.name);
        box->setChecked(m_checkedRemoteNames.contains(r.name));
        m_remoteChecksLayout->addWidget(box);
    }
}

void NewBoxDialog::openManageRemotes()
{
    syncCheckedRemoteNames(); // capture the user's ticks before the list is rebuilt
    ManageSshRemotesDialog dlg(this);
    dlg.exec();
    refreshRemoteList(); // pick up anything added/edited/removed, preserving this box's own checks
}

void NewBoxDialog::tryAccept()
{
    const QString dir = m_dirEdit->text().trimmed();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        QMessageBox::warning(this, "New Box", "Target directory does not exist.");
        return;
    }

    // The workspace checkbox is locked (and already satisfied) in edit
    // mode, so this only ever fires while actually choosing it.
    if (m_workspaceCheck->isEnabled() && m_workspaceCheck->isChecked()
        && slugify(m_nameEdit->text().trimmed()).isEmpty()) {
        QMessageBox::warning(this, "New Box",
                             "A workspace folder is named after the conversation, so give this "
                             "one a name (with at least one letter or digit) -- or untick the box "
                             "to work in the target directory itself.");
        return;
    }

    if (!m_editMode)
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

QStringList NewBoxDialog::sshRemoteRefs() const
{
    QStringList result;
    QSet<QString> shown;
    for (QCheckBox *box : m_remoteChecks->findChildren<QCheckBox *>()) {
        const QString name = box->property("remoteName").toString();
        if (name.isEmpty())
            continue;
        shown.insert(name);
        if (box->isChecked())
            result << name;
    }
    // A ref to a catalog entry that isn't shown right now (it was removed,
    // say) has no checkbox -- keep it as-is rather than letting an unrelated
    // edit to this box silently drop the attachment.
    for (const QString &name : m_checkedRemoteNames) {
        if (!shown.contains(name) && !result.contains(name))
            result << name;
    }
    return result;
}
