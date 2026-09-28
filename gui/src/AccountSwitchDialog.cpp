#include "AccountSwitchDialog.h"
#include "DockerBackend.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

AccountSwitchDialog::AccountSwitchDialog(DockerBackend *docker, QWidget *parent)
    : QDialog(parent), m_docker(docker)
{
    setWindowTitle(QStringLiteral("Claude Accounts"));
    setMinimumWidth(420);
    buildUi();
    refreshProfileList();
}

void AccountSwitchDialog::buildUi()
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setSpacing(10);
    vbox->setContentsMargins(16, 16, 16, 16);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    vbox->addWidget(m_statusLabel);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMaximumHeight(130);
    vbox->addWidget(m_list);

    m_noteLabel = new QLabel(this);
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setStyleSheet(QStringLiteral("color: #888;"));
    vbox->addWidget(m_noteLabel);

    vbox->addStretch();

    auto *hbox = new QHBoxLayout;
    hbox->addStretch();

    m_switchBtn = new QPushButton(QStringLiteral("Switch"), this);
    m_switchBtn->setEnabled(false);
    m_switchBtn->setDefault(true);
    hbox->addWidget(m_switchBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    hbox->addWidget(closeBtn);
    vbox->addLayout(hbox);

    connect(m_list, &QListWidget::currentRowChanged,
            this, &AccountSwitchDialog::onSelectionChanged);
    connect(m_switchBtn, &QPushButton::clicked,
            this, &AccountSwitchDialog::onSwitchClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void AccountSwitchDialog::refreshProfileList()
{
    const QString active = activeProfile();
    if (active.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("Active account: (unmanaged — ~/.claude is not a profile symlink)"));
    } else {
        m_statusLabel->setText(
            QString("Active: Account %1  (%2)")
            .arg(active, QDir::homePath() + "/.claude-" + active));
    }

    m_list->clear();
    const QStringList profiles = availableProfiles();
    for (const QString &p : profiles) {
        const bool isActive = (p == active);
        const QString label = QString("  %1     %2/.claude-%3  %4")
            .arg(p, QDir::homePath(), p, isActive ? QStringLiteral("(active)") : QString());
        auto *item = new QListWidgetItem(label.trimmed(), m_list);
        item->setData(Qt::UserRole, p);
        if (isActive) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }

    // Warn about running boxes that will be affected.
    const QList<BoxInfo> boxes = m_docker->listBoxes(/*sampleStats=*/false);
    int running = 0;
    for (const BoxInfo &b : boxes)
        if (b.status == BoxInfo::Status::Running)
            ++running;

    m_noteLabel->setText(running > 0
        ? QString("Switching stops and restarts %1 running box%2.")
              .arg(running).arg(running == 1 ? QString() : QStringLiteral("es"))
        : QStringLiteral("No running boxes will be affected."));

    onSelectionChanged();
}

void AccountSwitchDialog::onSelectionChanged()
{
    const QString active = activeProfile();
    const QListWidgetItem *item = m_list->currentItem();
    if (!item) {
        m_switchBtn->setEnabled(false);
        m_switchBtn->setText(QStringLiteral("Switch"));
        return;
    }
    const QString profile = item->data(Qt::UserRole).toString();
    const bool isSelf = (profile == active);
    m_switchBtn->setEnabled(!isSelf);
    m_switchBtn->setText(isSelf ? QStringLiteral("Active")
                                : QString("Switch to %1").arg(profile));
}

void AccountSwitchDialog::onSwitchClicked()
{
    const QListWidgetItem *item = m_list->currentItem();
    if (!item) return;
    const QString toProfile = item->data(Qt::UserRole).toString();

    // Snapshot running boxes before stopping them.
    const QList<BoxInfo> boxes = m_docker->listBoxes(/*sampleStats=*/false);
    QStringList runningNames, toStop;
    for (const BoxInfo &b : boxes) {
        if (b.status == BoxInfo::Status::Running) {
            runningNames << b.name;
            toStop       << b.name;
        }
    }

    if (!toStop.isEmpty()) {
        QHash<QString, QString> errors;
        m_docker->stopMany(toStop, &errors);
        if (!errors.isEmpty()) {
            QStringList msgs;
            for (auto it = errors.cbegin(); it != errors.cend(); ++it)
                msgs << it.key() + ": " + it.value();
            QMessageBox::warning(this, QStringLiteral("Switch"),
                QStringLiteral("Some boxes could not be stopped:\n") + msgs.join('\n'));
            return;
        }
    }

    QString error;
    if (!performSwitch(toProfile, &error)) {
        QMessageBox::critical(this, QStringLiteral("Switch Failed"), error);
        return;
    }

    emit switchCompleted(runningNames);
    accept();
}

// Port of the claude-account-switch bash script. Performs all filesystem
// operations needed to activate a profile: first-time init from the current
// ~/.claude, projects/ symlink setup, ~/.claude symlink swap, and the
// ~/.claude.json forwarding symlink. The container check from the script is
// handled by the caller (onSwitchClicked) before this is called.
bool AccountSwitchDialog::performSwitch(const QString &toProfile, QString *errorOut)
{
    const QString home       = QDir::homePath();
    const QString claudeDir  = home + "/.claude-" + toProfile;
    const QString sharedProj = home + "/.claude-projects-shared";
    const QString claudeLink = home + "/.claude";
    const QString claudeJson = home + "/.claude.json";

    auto fail = [&](const QString &msg) -> bool {
        if (errorOut) *errorOut = msg;
        return false;
    };

    // Ensure the shared projects store exists.
    if (!QDir().mkpath(sharedProj))
        return fail(QStringLiteral("Could not create ") + sharedProj);

    // First-time init: snapshot the current ~/.claude content into the profile.
    if (!QDir(claudeDir).exists()) {
        const QString source = QFileInfo(claudeLink).canonicalFilePath();
        if (source.isEmpty())
            return fail(QStringLiteral("~/.claude does not resolve to a real directory"));

        QProcess cp;
        cp.start(QStringLiteral("cp"), {QStringLiteral("-a"), source, claudeDir});
        if (!cp.waitForFinished(30000) || cp.exitCode() != 0)
            return fail(QStringLiteral("cp -a failed: ")
                        + QString::fromLocal8Bit(cp.readAllStandardError()));

        // Pull in .claude.json if not yet inside the profile.
        const QString srcJson = QFileInfo(claudeJson).canonicalFilePath();
        const QString dstJson = claudeDir + "/.claude.json";
        if (!srcJson.isEmpty() && !QFileInfo::exists(dstJson))
            QFile::copy(srcJson, dstJson);
    }

    // Ensure profile/projects is a symlink to the shared store.
    {
        const QFileInfo projectsInfo(claudeDir + "/projects");
        if (!projectsInfo.isSymLink()) {
            if (projectsInfo.isDir()) {
                // Migrate any existing transcripts then remove the directory.
                QProcess cp;
                cp.start(QStringLiteral("cp"),
                         {QStringLiteral("-an"),
                          claudeDir + "/projects/.",
                          sharedProj + "/"});
                cp.waitForFinished(30000);
                QDir(claudeDir + "/projects").removeRecursively();
            }
            if (!QFile::link(sharedProj, claudeDir + "/projects"))
                return fail(QString("Could not symlink %1/projects -> %2")
                            .arg(claudeDir, sharedProj));
        }
    }

    // Swap ~/.claude to the new profile. If it is currently a real directory
    // (first-ever run, never switched before), remove it -- its content was
    // already copied into the profile above.
    {
        const QFileInfo fi(claudeLink);
        if (fi.isDir() && !fi.isSymLink())
            QDir(claudeLink).removeRecursively();
        else
            QFile::remove(claudeLink);
    }
    if (!QFile::link(claudeDir, claudeLink))
        return fail(QStringLiteral("Could not create symlink ~/.claude -> ") + claudeDir);

    // ~/.claude.json: a permanent forwarding symlink created once.
    if (!QFileInfo(claudeJson).isSymLink()) {
        QFile::remove(claudeJson);
        if (!QFile::link(claudeDir + "/.claude.json", claudeJson))
            return fail(QStringLiteral("Could not create ~/.claude.json forwarding symlink"));
    }

    return true;
}

QString AccountSwitchDialog::activeProfile()
{
    const QFileInfo fi(QDir::homePath() + "/.claude");
    if (!fi.isSymLink()) return QString();
    const QString base = QFileInfo(fi.symLinkTarget()).fileName(); // e.g. ".claude-A"
    if (base.startsWith(QStringLiteral(".claude-")))
        return base.mid(8); // "A", "B", ...
    return QString();
}

QStringList AccountSwitchDialog::availableProfiles()
{
    QStringList result;
    const QFileInfoList entries = QDir(QDir::homePath()).entryInfoList(
        {QStringLiteral(".claude-*")},
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (name == QStringLiteral(".claude-projects-shared")) continue;
        if (!name.startsWith(QStringLiteral(".claude-")))      continue;
        const QString profile = name.mid(8);
        if (!profile.isEmpty())
            result << profile;
    }
    result.sort();
    return result;
}
