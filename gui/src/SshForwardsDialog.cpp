#include "SshForwardsDialog.h"

#include "CommandTerminalDialog.h"
#include "SetupWizard.h"
#include "Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

// Same small list-widget helpers NewBoxDialog uses for its port/mount
// rows -- duplicated rather than shared, same call as this app's other
// small per-file helpers (e.g. NewBoxDialog's own slugify()): the pieces
// are a few lines each and not worth a shared header over.
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

// Turns one "L:bindAddr:bindPort:destHost:destPort" (or "R:...") value
// into the friendly label shown in the forward list.
QString forwardLabel(const QString &value)
{
    const QStringList parts = value.split(':');
    if (parts.size() != 5)
        return value; // shouldn't happen, but show *something* rather than crash
    const QString dirLabel = parts.at(0) == QLatin1String("R") ? QStringLiteral("remote") : QStringLiteral("local");
    const QString bindLabel = parts.at(1).isEmpty() ? parts.at(2) : (parts.at(1) + ":" + parts.at(2));
    return QStringLiteral("%1  %2  →  %3:%4").arg(dirLabel, bindLabel, parts.at(3), parts.at(4));
}

// Mirrors SshTunnelSession's own splitHostPort() -- "user@host" or
// "user@host:port" -> host, port (-1 if none given). Duplicated rather
// than shared for the same reason as the small helpers above.
void splitHostPort(const QString &raw, QString &hostOut, int &portOut)
{
    hostOut = raw;
    portOut = -1;

    const int at = raw.lastIndexOf('@');
    const int colon = raw.lastIndexOf(':');
    if (colon <= at)
        return;

    bool ok = false;
    const int port = raw.mid(colon + 1).toInt(&ok);
    if (!ok || port <= 0 || port > 65535)
        return;

    hostOut = raw.left(colon);
    portOut = port;
}

} // namespace

SshForwardsDialog::SshForwardsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("SSH Forwards");
    resize(520, 420);

    auto *mainLayout = new QVBoxLayout(this);

    auto *targetRow = new QGridLayout();
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText("user@host[:port]");
    targetRow->addWidget(new QLabel("SSH target:", this), 0, 0);
    targetRow->addWidget(m_hostEdit, 0, 1, 1, 2);
    m_identityEdit = new QLineEdit(this);
    m_identityEdit->setPlaceholderText("default identity / ssh-agent");
    auto *identityBrowse = new QPushButton("Browse…", this);
    targetRow->addWidget(new QLabel("Identity file:", this), 1, 0);
    targetRow->addWidget(m_identityEdit, 1, 1);
    targetRow->addWidget(identityBrowse, 1, 2);
    mainLayout->addLayout(targetRow);

    auto *testRow = new QHBoxLayout();
    m_connectionStatus = new QLabel(QStringLiteral("Not tested."), this);
    m_connectionStatus->setWordWrap(true);
    m_connectionStatus->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    m_testButton = new QPushButton("Test Connection", this);
    connect(m_testButton, &QPushButton::clicked, this, &SshForwardsDialog::testConnection);
    testRow->addWidget(m_connectionStatus, 1);
    testRow->addWidget(m_testButton);
    // A stale "works!" from before the target was edited is worse than no
    // status at all -- clear it the moment either field changes.
    auto resetConnectionStatus = [this] {
        m_connectionStatus->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
        m_connectionStatus->setText(QStringLiteral("Not tested."));
    };
    connect(m_hostEdit, &QLineEdit::textChanged, this, resetConnectionStatus);
    connect(m_identityEdit, &QLineEdit::textChanged, this, resetConnectionStatus);
    mainLayout->addLayout(testRow);

    auto *hint = new QLabel(
        "Runs as a background ssh process on the host (not inside the box), so it can reach "
        "docker-internal addresses -- the bridge gateway, a sibling container's own IP -- that "
        "only mean something from here. Key-based auth only: there's no terminal for it to "
        "prompt on, so an agent or an unlocked identity file is required.", this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::dimText().name()));
    mainLayout->addWidget(hint);

    m_forwardList = new QListWidget(this);
    mainLayout->addWidget(m_forwardList, 1);

    auto *forwardRow = new QGridLayout();
    m_dirCombo = new QComboBox(this);
    m_dirCombo->addItem("Local (-L)", QStringLiteral("L"));
    m_dirCombo->addItem("Remote (-R)", QStringLiteral("R"));
    m_bindEdit = new QLineEdit(this);
    m_bindPortEdit = new QLineEdit(this);
    m_bindPortEdit->setValidator(new QIntValidator(1, 65535, this));
    m_bindPortEdit->setPlaceholderText("bind port");
    m_destHostEdit = new QLineEdit(this);
    m_destHostEdit->setPlaceholderText("dest host");
    m_destPortEdit = new QLineEdit(this);
    m_destPortEdit->setValidator(new QIntValidator(1, 65535, this));
    m_destPortEdit->setPlaceholderText("dest port");
    forwardRow->addWidget(m_dirCombo, 0, 0);
    forwardRow->addWidget(m_bindEdit, 0, 1);
    forwardRow->addWidget(m_bindPortEdit, 0, 2);
    forwardRow->addWidget(new QLabel("→", this), 0, 3);
    forwardRow->addWidget(m_destHostEdit, 0, 4);
    forwardRow->addWidget(m_destPortEdit, 0, 5);
    mainLayout->addLayout(forwardRow);

    auto *forwardButtons = new QHBoxLayout();
    auto *addButton = new QPushButton("Add", this);
    auto *removeButton = new QPushButton("Remove Selected", this);
    forwardButtons->addStretch();
    forwardButtons->addWidget(addButton);
    forwardButtons->addWidget(removeButton);
    mainLayout->addLayout(forwardButtons);

    // The bind address means a different machine depending on direction --
    // an interface on *this* host for -L, on the *remote* one for -R -- and
    // that mix-up is exactly the mistake this placeholder exists to head
    // off: a docker-internal address like a container's own IP belongs in
    // "dest host" always, never here, and for -R this field is usually
    // just left blank.
    auto updateBindPlaceholder = [this] {
        m_bindEdit->setPlaceholderText(m_dirCombo->currentData().toString() == QLatin1String("R")
            ? QStringLiteral("bind addr on the REMOTE host (rare; usually blank)")
            : QStringLiteral("bind addr on THIS host (optional)"));
    };
    updateBindPlaceholder();
    connect(m_dirCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateBindPlaceholder);

    connect(identityBrowse, &QPushButton::clicked, this, &SshForwardsDialog::browseForIdentity);
    connect(addButton, &QPushButton::clicked, this, &SshForwardsDialog::addForward);
    connect(removeButton, &QPushButton::clicked, this, &SshForwardsDialog::removeSelectedForward);
    connect(m_bindPortEdit, &QLineEdit::returnPressed, this, &SshForwardsDialog::addForward);
    connect(m_destPortEdit, &QLineEdit::returnPressed, this, &SshForwardsDialog::addForward);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SshForwardsDialog::tryAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SshForwardsDialog::reject);
    mainLayout->addWidget(buttons);
}

void SshForwardsDialog::setConfig(const QString &host, const QString &identity, const QStringList &forwards)
{
    m_hostEdit->setText(host);
    m_identityEdit->setText(identity);
    m_forwardList->clear();
    for (const QString &fwd : forwards)
        addListValue(m_forwardList, forwardLabel(fwd), fwd);
}

void SshForwardsDialog::browseForIdentity()
{
    const QString start = m_identityEdit->text().trimmed().isEmpty()
        ? QDir::homePath() + "/.ssh" : m_identityEdit->text().trimmed();
    const QString file = QFileDialog::getOpenFileName(this, "Select SSH Identity File", start);
    if (!file.isEmpty())
        m_identityEdit->setText(file);
}

void SshForwardsDialog::addForward()
{
    const QString bindPort = m_bindPortEdit->text().trimmed();
    const QString destHost = m_destHostEdit->text().trimmed();
    const QString destPort = m_destPortEdit->text().trimmed();

    if (bindPort.isEmpty() || destHost.isEmpty() || destPort.isEmpty()) {
        QMessageBox::warning(this, "SSH forward",
                             "Enter at least the bind port, destination host, and destination port.");
        return;
    }

    const QString dir = m_dirCombo->currentData().toString();
    const QString bindAddr = m_bindEdit->text().trimmed();

    // The mistake this app just watched someone make: a remote bind
    // address identical to the destination host is almost always the
    // destination typed twice, not a deliberate remote-side bind -- that
    // address (typically something docker-internal, like a container's
    // own IP) doesn't exist on the far end at all.
    if (dir == QLatin1String("R") && !bindAddr.isEmpty() && bindAddr == destHost) {
        if (QMessageBox::question(this, "SSH forward",
                QStringLiteral("Bind address and destination host are both %1.\n\n"
                    "For a remote forward, the bind address is an interface on the *remote* "
                    "machine -- %1 almost certainly doesn't exist there; it belongs in "
                    "\"dest host\" only. Add it as typed anyway?").arg(destHost))
            != QMessageBox::Yes)
            return;
    }

    const QString value = dir + ":" + bindAddr + ":" + bindPort + ":" + destHost + ":" + destPort;

    if (!listContainsValue(m_forwardList, value))
        addListValue(m_forwardList, forwardLabel(value), value);

    m_bindEdit->clear();
    m_bindPortEdit->clear();
    m_destHostEdit->clear();
    m_destPortEdit->clear();
    m_bindPortEdit->setFocus();
}

void SshForwardsDialog::removeSelectedForward()
{
    const int row = m_forwardList->currentRow();
    if (row >= 0)
        delete m_forwardList->takeItem(row);
}

// Non-interactive: BatchMode=yes means this can never block on a prompt,
// so it either succeeds, fails on auth, or fails to reach the host at
// all -- distinguishing those is the whole point, since only the auth
// case is something offerInteractiveLogin() can actually fix.
SshForwardsDialog::ConnectResult SshForwardsDialog::probeConnection() const
{
    const QString target = m_hostEdit->text().trimmed();
    if (target.isEmpty())
        return {ConnectResult::Outcome::Unreachable, QStringLiteral("No SSH target set.")};

    QString host;
    int port = -1;
    splitHostPort(target, host, port);

    QStringList args;
    args << "-o" << "BatchMode=yes" << "-o" << "ConnectTimeout=6"
         << "-o" << "StrictHostKeyChecking=accept-new";
    const QString identity = m_identityEdit->text().trimmed();
    if (!identity.isEmpty())
        args << "-i" << identity;
    if (port > 0)
        args << "-p" << QString::number(port);
    args << host << "true";

    QProcess p;
    p.start(QStringLiteral("ssh"), args);
    if (!p.waitForStarted(3000))
        return {ConnectResult::Outcome::Unreachable, QStringLiteral("Couldn't start ssh -- is it installed?")};
    p.waitForFinished(10000);

    const QString stderrText = QString::fromUtf8(p.readAllStandardError()).trimmed();
    if (p.exitCode() == 0)
        return {ConnectResult::Outcome::Ok, QStringLiteral("Key-based login works.")};

    // BatchMode's own refusal to prompt reads as "Permission denied
    // (publickey...)" on stderr -- that specific case is fixable by
    // logging in once interactively to install the key; anything else
    // (unknown host, connection refused/timed out, DNS failure) isn't
    // something a password prompt would help with.
    if (stderrText.contains(QStringLiteral("Permission denied")))
        return {ConnectResult::Outcome::AuthFailed, stderrText};
    return {ConnectResult::Outcome::Unreachable,
            stderrText.isEmpty() ? QStringLiteral("ssh exited %1.").arg(p.exitCode()) : stderrText};
}

void SshForwardsDialog::testConnection()
{
    m_connectionStatus->setText(QStringLiteral("Testing…"));
    m_testButton->setEnabled(false);
    // Let the "Testing…" text actually paint before the (up to ~10s)
    // blocking probe below -- same tradeoff DockerBackend's synchronous
    // calls already make for one-off admin actions in this app, not
    // something worth a worker thread for a button click.
    repaint();

    const ConnectResult result = probeConnection();
    m_testButton->setEnabled(true);

    switch (result.outcome) {
    case ConnectResult::Outcome::Ok:
        m_connectionStatus->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::running().name()));
        m_connectionStatus->setText(result.detail);
        break;
    case ConnectResult::Outcome::AuthFailed:
        m_connectionStatus->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::stopped().name()));
        m_connectionStatus->setText(QStringLiteral("Key-based login isn't set up yet."));
        offerInteractiveLogin();
        break;
    case ConnectResult::Outcome::Unreachable:
        m_connectionStatus->setStyleSheet(QStringLiteral("color: %1;").arg(Theme::stopped().name()));
        m_connectionStatus->setText(result.detail);
        break;
    }
}

// Opens a real terminal (CommandTerminalDialog, embedding TerminalWidget)
// running ssh-copy-id so the user can type a password once and install
// this box's public key on the remote -- the one part of this flow that
// fundamentally can't be automated, since BatchMode means the ordinary
// non-interactive path can never itself ask for one.
void SshForwardsDialog::offerInteractiveLogin()
{
    const QString target = m_hostEdit->text().trimmed();
    QString host;
    int port = -1;
    splitHostPort(target, host, port);

    // Prefer this box's own identity; fall back to the app-wide default
    // (see SetupWizard) so there's still something to install even when
    // this particular box hasn't set one.
    QString identity = m_identityEdit->text().trimmed();
    if (identity.isEmpty())
        identity = SetupWizard::defaultSshIdentity();

    const bool haveCopyId = !QStandardPaths::findExecutable(QStringLiteral("ssh-copy-id")).isEmpty();
    const QString question = haveCopyId && !identity.isEmpty()
        ? QStringLiteral("Open a terminal to log in to %1 with a password and install %2 there?")
              .arg(target, identity)
        : QStringLiteral("Open a terminal to log in to %1 with a password? "
                         "(No identity file to install automatically -- %2set one above, or in Setup, "
                         "and this can copy it next time.)")
              .arg(target, haveCopyId ? QString() : QStringLiteral("ssh-copy-id isn't installed, and "));
    if (QMessageBox::question(this, "SSH forward", question) != QMessageBox::Yes)
        return;

    QString program;
    QStringList args;
    if (haveCopyId && !identity.isEmpty()) {
        program = QStringLiteral("ssh-copy-id");
        args << "-i" << identity;
        if (port > 0)
            args << "-p" << QString::number(port);
        args << host;
    } else {
        // Nothing to install automatically -- still worth opening an
        // interactive session so the user can log in and sort out
        // access by hand (e.g. append a pubkey to authorized_keys
        // themselves).
        program = QStringLiteral("ssh");
        args << "-o" << "StrictHostKeyChecking=accept-new";
        if (port > 0)
            args << "-p" << QString::number(port);
        args << host;
    }

    auto *dlg = new CommandTerminalDialog(QStringLiteral("SSH Login — ") + target, program, args, this);
    dlg->exec();
    dlg->deleteLater();

    // Automatically re-check rather than leaving the stale "isn't set up
    // yet" status showing after a login that may well have just fixed it.
    testConnection();
}

void SshForwardsDialog::tryAccept()
{
    if (!listValues(m_forwardList).isEmpty() && m_hostEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "SSH forward",
                             "Add an SSH target (user@host) for the configured forward(s), or remove them.");
        return;
    }
    accept();
}

QString SshForwardsDialog::sshHost() const
{
    return m_hostEdit->text().trimmed();
}

QString SshForwardsDialog::sshIdentity() const
{
    return m_identityEdit->text().trimmed();
}

QStringList SshForwardsDialog::sshForwards() const
{
    return listValues(m_forwardList);
}
