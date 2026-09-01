#include "SshForwardsDialog.h"

#include "Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
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
    m_bindEdit->setPlaceholderText("bind addr (optional)");
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
