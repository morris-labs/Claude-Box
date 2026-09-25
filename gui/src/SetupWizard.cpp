#include "SetupWizard.h"

#include "CommandTerminalDialog.h"
#include "DockerApi.h"
#include "DockerBackend.h"
#include "Icons.h"
#include "Theme.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

const char *kCompletedKey = "setup/completed";
const char *kTargetDirKey = "setup/defaultTargetDir";
const char *kSshIdentityKey = "setup/sshIdentity";

QString dimStyle()
{
    return QStringLiteral("color: %1;").arg(Theme::dimText().name());
}

// True if group/other has any access at all -- the thing ssh actually
// cares about for ~/.ssh and private key files (it refuses, or at least
// loudly warns, the moment either is group/other-readable).
bool groupOrOtherHaveAccess(const QFileInfo &info)
{
    return info.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                                | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther);
}

} // namespace

QString SetupWizard::defaultTargetDir()
{
    const QString configured = QSettings().value(kTargetDirKey).toString();
    if (!configured.isEmpty() && QDir(configured).exists())
        return configured;
    return QDir::homePath();
}

QString SetupWizard::defaultSshIdentity()
{
    const QString configured = QSettings().value(kSshIdentityKey).toString();
    if (!configured.isEmpty() && QFileInfo::exists(configured) && QFileInfo::exists(configured + ".pub"))
        return configured;

    // Not configured (or the configured one is gone) -- fall back to
    // whatever's already sitting under ~/.ssh, same preference order ssh
    // itself uses.
    const QString sshDir = QDir::homePath() + "/.ssh";
    for (const QString &name : {QStringLiteral("id_ed25519"), QStringLiteral("id_rsa")}) {
        const QString candidate = sshDir + "/" + name;
        if (QFileInfo::exists(candidate) && QFileInfo::exists(candidate + ".pub"))
            return candidate;
    }
    return QString();
}

bool SetupWizard::hasCompletedSetup()
{
    return QSettings().value(kCompletedKey, false).toBool();
}

SetupWizard::SetupWizard(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("claude-box Setup");
    resize(640, 560);

    // Same lesson as NewBoxDialog: content in a scroll area, Close/Done
    // pinned outside it, so this dialog's footprint is always exactly
    // what resize() asks for regardless of how many checks it grows to
    // hold later. Horizontal scrolling off for the same reason as there
    // too -- see NewBoxDialog's constructor comment.
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

    auto *intro = new QLabel(
        "Checks the things claude-box needs and isn't shipped with: Docker itself, the "
        "claude-code image, and an SSH keypair for boxes that use SSH forwards. Nothing here "
        "is required to close this -- fix what applies to you, skip the rest.", this);
    intro->setWordWrap(true);
    intro->setStyleSheet(dimStyle());
    mainLayout->addWidget(intro);

    auto *grid = new QGridLayout();
    grid->setColumnStretch(1, 1);
    int row = 0;

    m_dockerRow = addCheckRow(grid, row++, "Docker");

    m_buildImageButton = new QPushButton("Build Image", this);
    connect(m_buildImageButton, &QPushButton::clicked, this, &SetupWizard::buildDockerImage);
    m_imageRow = addCheckRow(grid, row++, "claude-code image", m_buildImageButton);

    m_sshClientRow = addCheckRow(grid, row++, "SSH client");

    m_generateKeyButton = new QPushButton("Generate Keypair", this);
    connect(m_generateKeyButton, &QPushButton::clicked, this, &SetupWizard::generateSshKey);
    m_sshKeyRow = addCheckRow(grid, row++, "SSH keypair", m_generateKeyButton);

#ifndef Q_OS_WIN
    m_fixPermButton = new QPushButton("Fix Permissions", this);
    connect(m_fixPermButton, &QPushButton::clicked, this, &SetupWizard::fixSshPermissions);
    m_sshPermRow = addCheckRow(grid, row++, "SSH permissions", m_fixPermButton);
#endif

    mainLayout->addLayout(grid);

    auto *recheckRow = new QHBoxLayout();
    recheckRow->addStretch();
    auto *recheckButton = new QPushButton("Recheck", this);
    connect(recheckButton, &QPushButton::clicked, this, &SetupWizard::refreshChecks);
    recheckRow->addWidget(recheckButton);
    mainLayout->addLayout(recheckRow);

    auto *dirGroup = new QLabel("Default directory for New Box:", this);
    mainLayout->addWidget(dirGroup);
    auto *dirRow = new QHBoxLayout();
    m_targetDirEdit = new QLineEdit(defaultTargetDir(), this);
    auto *dirBrowse = new QPushButton("Browse…", this);
    connect(dirBrowse, &QPushButton::clicked, this, &SetupWizard::browseDefaultDir);
    dirRow->addWidget(m_targetDirEdit);
    dirRow->addWidget(dirBrowse);
    mainLayout->addLayout(dirRow);

    mainLayout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText("Done");
    connect(buttons, &QDialogButtonBox::rejected, this, &SetupWizard::finish);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &SetupWizard::finish);
    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(9, 6, 9, 9);
    buttonRow->addWidget(buttons);
    outerLayout->addLayout(buttonRow);

    refreshChecks();
}

SetupWizard::CheckRow SetupWizard::addCheckRow(QGridLayout *grid, int row, const QString &label,
                                               QPushButton *action)
{
    CheckRow r;
    r.dot = new QLabel(this);
    r.dot->setFixedWidth(16);
    grid->addWidget(r.dot, row, 0, Qt::AlignTop);

    auto *textCol = new QVBoxLayout();
    auto *labelWidget = new QLabel(label, this);
    QFont bold = labelWidget->font();
    bold.setBold(true);
    labelWidget->setFont(bold);
    textCol->addWidget(labelWidget);
    r.detail = new QLabel(this);
    r.detail->setWordWrap(true);
    r.detail->setStyleSheet(dimStyle());
    textCol->addWidget(r.detail);
    grid->addLayout(textCol, row, 1);

    if (action)
        grid->addWidget(action, row, 2, Qt::AlignTop);

    return r;
}

void SetupWizard::setRowStatus(const CheckRow &row, bool ok, const QString &detail)
{
    row.dot->setPixmap(Icons::statusDot(ok ? Theme::running() : Theme::stopped()).pixmap(12, 12));
    row.detail->setText(detail);
}

void SetupWizard::refreshChecks()
{
    // Docker.
    if (DockerApi::isAvailable()) {
        setRowStatus(m_dockerRow, true, "Reachable via the Docker Engine API.");
    } else {
        QProcess p;
        p.start(QStringLiteral("docker"), {QStringLiteral("info")});
        const bool ok = p.waitForStarted(3000) && p.waitForFinished(8000) && p.exitCode() == 0;
        setRowStatus(m_dockerRow, ok,
            ok ? "Reachable via the docker CLI (API socket not available)."
               : "Not reachable. Install Docker (Desktop, or docker-ce) and make sure it's running.");
    }

    // claude-code image.
    bool imageOk = false;
    if (DockerApi::isAvailable()) {
        QString err;
        const QJsonDocument doc = DockerApi::get(
            QStringLiteral("/") + DockerApi::kApiVersion + "/images/claude-code/json", &err);
        imageOk = !doc.isNull();
    } else {
        QProcess p;
        p.start(QStringLiteral("docker"),
               {QStringLiteral("image"), QStringLiteral("inspect"), QStringLiteral("claude-code")});
        imageOk = p.waitForStarted(3000) && p.waitForFinished(8000) && p.exitCode() == 0;
    }
    setRowStatus(m_imageRow, imageOk,
        imageOk ? "Built." : "Not built yet -- boxes can't start without it.");
    m_buildImageButton->setText(imageOk ? "Rebuild Image" : "Build Image");
    m_buildImageButton->setEnabled(true);

    // SSH client.
    const QString sshPath = QStandardPaths::findExecutable(QStringLiteral("ssh"));
    setRowStatus(m_sshClientRow, !sshPath.isEmpty(),
        sshPath.isEmpty()
            ? "No `ssh` on PATH -- SSH forwards (Edit Box) won't work until an OpenSSH client is installed."
            : sshPath);

    // SSH keypair.
    const QString identity = defaultSshIdentity();
    setRowStatus(m_sshKeyRow, !identity.isEmpty(),
        identity.isEmpty() ? "No default keypair configured or found under ~/.ssh." : identity);
    m_generateKeyButton->setEnabled(identity.isEmpty());

#ifndef Q_OS_WIN
    // SSH permissions -- ssh itself refuses (or loudly warns about) a
    // group/other-accessible ~/.ssh or private key.
    const QString sshDir = QDir::homePath() + "/.ssh";
    const QFileInfo dirInfo(sshDir);
    bool permOk = true;
    QString permDetail;
    if (!dirInfo.exists()) {
        permOk = false;
        permDetail = "~/.ssh doesn't exist yet.";
    } else if (groupOrOtherHaveAccess(dirInfo)) {
        permOk = false;
        permDetail = "~/.ssh is group/other-accessible.";
    } else if (!identity.isEmpty() && groupOrOtherHaveAccess(QFileInfo(identity))) {
        permOk = false;
        permDetail = QStringLiteral("%1 is group/other-readable.").arg(identity);
    } else {
        permDetail = identity.isEmpty()
            ? "~/.ssh is owner-only." : "~/.ssh and the default key are owner-only.";
    }
    setRowStatus(m_sshPermRow, permOk, permDetail);
    if (m_fixPermButton)
        m_fixPermButton->setEnabled(!permOk);
#endif
}

void SetupWizard::browseDefaultDir()
{
    const QString start = m_targetDirEdit->text().trimmed().isEmpty()
        ? QDir::homePath() : m_targetDirEdit->text().trimmed();
    const QString dir = QFileDialog::getExistingDirectory(this, "Default Directory for New Box", start);
    if (!dir.isEmpty())
        m_targetDirEdit->setText(dir);
}

void SetupWizard::buildDockerImage()
{
#ifdef CLAUDE_BOX_REPO_DIR
    const QString repoDir = QStringLiteral(CLAUDE_BOX_REPO_DIR);
#else
    const QString repoDir;
#endif
    const QString userName = DockerBackend::containerUsername();
    if (repoDir.isEmpty() || !QFileInfo::exists(repoDir + "/Dockerfile")) {
        QMessageBox::information(this, "Build Image",
            "This build of claude-box-gui doesn't know where its own Dockerfile is (it wasn't "
            "built from a source checkout, or the checkout has since moved) -- build it "
            "yourself:\n\ndocker build -t claude-code --build-arg USER_NAME=" + userName +
            " <path to the claude-box repo>");
        return;
    }

    // --build-arg USER_NAME must match what DockerBackend::launchSpec() later
    // passes to `docker run --user`: the image's uid-1000 account is renamed
    // to this value at build time, and a run with a different --user finds no
    // matching passwd entry.
    auto *dlg = new CommandTerminalDialog(QStringLiteral("Building claude-code image"),
                                          QStringLiteral("docker"),
                                          {QStringLiteral("build"), QStringLiteral("-t"),
                                           QStringLiteral("claude-code"),
                                           QStringLiteral("--build-arg"),
                                           QStringLiteral("USER_NAME=") + userName, repoDir},
                                          this);
    dlg->exec();
    dlg->deleteLater();
    refreshChecks();
}

void SetupWizard::generateSshKey()
{
    const QString sshDir = QDir::homePath() + "/.ssh";
    QDir().mkpath(sshDir);
#ifndef Q_OS_WIN
    QFile::setPermissions(sshDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif

    const QString path = sshDir + "/id_ed25519";
    if (!QFileInfo::exists(path)) {
        QProcess p;
        // -N "" -- an empty passphrase, not "no argument given": these
        // tunnels run headless (BatchMode=yes, see SshTunnelSession), so a
        // passphrase-protected key would just never work outside an
        // already-unlocked agent.
        p.start(QStringLiteral("ssh-keygen"),
               {QStringLiteral("-t"), QStringLiteral("ed25519"),
                QStringLiteral("-N"), QString(),
                QStringLiteral("-f"), path,
                QStringLiteral("-C"), QStringLiteral("claude-box")});
        if (!p.waitForStarted(3000) || !p.waitForFinished(15000) || p.exitCode() != 0) {
            QMessageBox::warning(this, "Generate Keypair",
                                 "ssh-keygen failed:\n" + QString::fromUtf8(p.readAllStandardError()));
            return;
        }
    }

    QSettings().setValue(kSshIdentityKey, path);
    refreshChecks();
}

void SetupWizard::fixSshPermissions()
{
#ifndef Q_OS_WIN
    const QString sshDir = QDir::homePath() + "/.ssh";
    QFile::setPermissions(sshDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    const QString identity = defaultSshIdentity();
    if (!identity.isEmpty())
        QFile::setPermissions(identity, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    refreshChecks();
#endif
}

void SetupWizard::finish()
{
    QSettings().setValue(kTargetDirKey, m_targetDirEdit->text().trimmed());
    QSettings().setValue(kCompletedKey, true);
    accept();
}
