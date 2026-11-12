#include "dialogs.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace gds {

namespace {
    enum { IdRole = Qt::UserRole + 1, LoadedRole, PlaceholderRole };
}

// ---------------------------------------------------------------- credentials

CredentialsDialog::CredentialsDialog(const QString& clientId, const QString& clientSecret, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Google API Client"));
    auto* layout = new QVBoxLayout(this);
    // Where the two values are found in the Google Cloud console
    // (https://developers.google.com/workspace/guides/create-credentials).
    auto* intro = new QLabel(
        i18n(
            "<p>Where to find the client ID and client secret:</p>"
            "<ol>"
            "<li>Open <a href=\"https://console.cloud.google.com/auth/clients\">console.cloud.google.com</a> "
            "and select your project.</li>"
            "<li>Go to <b>Menu &gt; Google Auth platform &gt; Clients</b>.</li>"
            "<li>Click your client of type <b>Desktop app</b>, or create one with <b>Create Client</b>.</li>"
            "<li>Copy <b>Client ID</b> and <b>Client secret</b> from the client's page.</li>"
            "</ol>"
            "<p>The Google Drive API must be enabled for the project. The values are stored in the KDE "
            "wallet.</p>"),
        this);
    intro->setObjectName(QStringLiteral("credentialsHelp"));
    intro->setTextFormat(Qt::RichText);
    intro->setOpenExternalLinks(true);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* form = new QFormLayout;
    m_id = new QLineEdit(clientId, this);
    m_id->setObjectName(QStringLiteral("clientId"));
    m_secret = new QLineEdit(clientSecret, this);
    m_secret->setObjectName(QStringLiteral("clientSecret"));
    m_secret->setEchoMode(QLineEdit::Password);
    form->addRow(i18n("Client ID:"), m_id);
    form->addRow(i18n("Client secret:"), m_secret);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    QPushButton* ok = buttons->button(QDialogButtonBox::Ok);
    const auto update = [this, ok]() { ok->setEnabled(!m_id->text().trimmed().isEmpty()); };
    connect(m_id, &QLineEdit::textChanged, this, update);
    update();
    resize(520, sizeHint().height());
}

QString CredentialsDialog::clientId() const
{
    return m_id->text().trimmed();
}

QString CredentialsDialog::clientSecret() const
{
    return m_secret->text().trimmed();
}

// ------------------------------------------------------------- automatic sync

AutoSyncDialog::AutoSyncDialog(bool enabled, int intervalMinutes, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Automatic Sync"));
    auto* layout = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    m_enabled = new QCheckBox(i18n("Sync all folders automatically every"), this);
    m_enabled->setObjectName(QStringLiteral("autoSyncCheck"));
    m_enabled->setChecked(enabled);
    m_interval = new QSpinBox(this);
    m_interval->setObjectName(QStringLiteral("autoSyncInterval"));
    m_interval->setRange(5, 24 * 60);
    m_interval->setValue(intervalMinutes > 0 ? intervalMinutes : 30);
    m_interval->setSuffix(i18n(" minutes"));
    row->addWidget(m_enabled);
    row->addWidget(m_interval);
    row->addStretch(1);
    layout->addLayout(row);
    auto* note = new QLabel(
        i18n("Installs a systemd user timer. Automatic runs never delete files and never resolve "
             "conflicts; those are left for the next manual sync."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(m_enabled, &QCheckBox::toggled, m_interval, &QWidget::setEnabled);
    m_interval->setEnabled(enabled);
    resize(480, sizeHint().height());
}

bool AutoSyncDialog::isEnabledChecked() const
{
    return m_enabled->isChecked();
}

int AutoSyncDialog::intervalMinutes() const
{
    return m_interval->value();
}

// ------------------------------------------------------------------ direction

DirectionDialog::DirectionDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Sync Direction"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        i18n("Choose how files are synced. The choice applies to all folders until you sign out."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_twoWay = new QRadioButton(i18n("Two-way: changes and deletions are applied in both directions"), this);
    m_twoWay->setObjectName(QStringLiteral("twoWay"));
    m_driveToLocal = new QRadioButton(i18n("Google Drive to local: local changes are never uploaded"), this);
    m_driveToLocal->setObjectName(QStringLiteral("driveToLocal"));
    m_localToDrive
        = new QRadioButton(i18n("Local to Google Drive: changes on Drive are never downloaded"), this);
    m_localToDrive->setObjectName(QStringLiteral("localToDrive"));
    m_twoWay->setChecked(true);
    layout->addWidget(m_twoWay);
    layout->addWidget(m_driveToLocal);
    layout->addWidget(m_localToDrive);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

SyncDirection DirectionDialog::direction() const
{
    if (m_driveToLocal->isChecked())
        return SyncDirection::DriveToLocal;
    if (m_localToDrive->isChecked())
        return SyncDirection::LocalToDrive;
    return SyncDirection::TwoWay;
}

void DirectionDialog::setDirection(SyncDirection direction)
{
    m_driveToLocal->setChecked(direction == SyncDirection::DriveToLocal);
    m_localToDrive->setChecked(direction == SyncDirection::LocalToDrive);
    m_twoWay->setChecked(
        direction != SyncDirection::DriveToLocal && direction != SyncDirection::LocalToDrive);
}

// ----------------------------------------------------------------- add folder

AddFolderDialog::AddFolderDialog(AppCore* core, QWidget* parent)
    : QDialog(parent)
    , m_core(core)
{
    setWindowTitle(i18nc("@title:window", "Add Folder"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(i18n("Google Drive folder to sync:"), this));
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("driveTree"));
    m_tree->setHeaderHidden(true);
    layout->addWidget(m_tree, 1);

    layout->addWidget(new QLabel(i18n("Local destination (must be an empty folder):"), this));
    auto* row = new QHBoxLayout;
    m_path = new QLineEdit(this);
    m_path->setObjectName(QStringLiteral("localPath"));
    auto* browseButton
        = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Browse"), this);
    row->addWidget(m_path, 1);
    row->addWidget(browseButton);
    layout->addLayout(row);
    m_validation = new QLabel(this);
    m_validation->setObjectName(QStringLiteral("validation"));
    m_validation->setWordWrap(true);
    layout->addWidget(m_validation);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    auto* root = new QTreeWidgetItem(m_tree, {i18n("My Drive")});
    root->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-cloud")));
    root->setData(0, IdRole, QStringLiteral("root"));
    m_items.insert(QStringLiteral("root"), root);

    connect(m_tree, &QTreeWidget::itemExpanded, this, &AddFolderDialog::loadChildren);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &AddFolderDialog::validate);
    connect(m_path, &QLineEdit::textChanged, this, &AddFolderDialog::validate);
    connect(browseButton, &QPushButton::clicked, this, &AddFolderDialog::browse);
    connect(m_core, &AppCore::driveFoldersListed, this, &AddFolderDialog::onListed);

    loadChildren(root);
    root->setExpanded(true);
    validate();
    resize(560, 480);
}

void AddFolderDialog::loadChildren(QTreeWidgetItem* item)
{
    if (item->data(0, LoadedRole).toBool())
        return;
    item->setData(0, LoadedRole, true);
    qDeleteAll(item->takeChildren());
    auto* placeholder = new QTreeWidgetItem(item, {i18n("Loading…")});
    placeholder->setData(0, PlaceholderRole, true);
    placeholder->setFlags(Qt::NoItemFlags);
    m_core->listDriveFolders(item->data(0, IdRole).toString()); // answered by onListed()
}

void AddFolderDialog::onListed(
    const QString& parentId, const QVector<DriveItem>& folders, const QString& error)
{
    QTreeWidgetItem* parent = m_items.value(parentId);
    if (!parent)
        return;
    qDeleteAll(parent->takeChildren());
    if (!error.isEmpty()) {
        auto* failed = new QTreeWidgetItem(parent, {i18n("Could not load: %1", error)});
        failed->setData(0, PlaceholderRole, true);
        failed->setFlags(Qt::NoItemFlags);
        parent->setData(0, LoadedRole, false); // retry on the next expansion
        return;
    }
    for (const DriveItem& folder : folders) {
        auto* item = new QTreeWidgetItem(parent, {folder.name});
        item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
        item->setData(0, IdRole, folder.id);
        // sub-folders are fetched when the item is expanded
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        m_items.insert(folder.id, item);
    }
}

QString AddFolderDialog::driveFolderId() const
{
    const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
    if (selected.isEmpty() || selected.first()->data(0, PlaceholderRole).toBool())
        return {};
    const QString id = selected.first()->data(0, IdRole).toString();
    // The whole drive cannot be mounted, only a folder of it.
    return id == QLatin1String("root") ? QString() : id;
}

QString AddFolderDialog::driveFolderName() const
{
    const QList<QTreeWidgetItem*> selected = m_tree->selectedItems();
    return selected.isEmpty() ? QString() : selected.first()->text(0);
}

QString AddFolderDialog::localPath() const
{
    return m_path->text().trimmed();
}

void AddFolderDialog::setLocalPath(const QString& path)
{
    m_path->setText(path);
}

void AddFolderDialog::browse()
{
    auto* dialog = new QFileDialog(this, i18nc("@title:window", "Select an Empty Destination Folder"),
        localPath().isEmpty() ? QDir::homePath() : localPath());
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QFileDialog::fileSelected, this, &AddFolderDialog::setLocalPath);
    dialog->open();
}

void AddFolderDialog::validate()
{
    QString problem;
    if (driveFolderId().isEmpty())
        problem = i18n("Select a folder of your Google Drive (the whole drive cannot be mounted).");
    else if (localPath().isEmpty())
        problem = i18n("Choose a local destination folder.");
    else
        problem = m_core->folders()->validateDestination(localPath());
    m_validation->setText(problem);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

// ------------------------------------------------------------------ deletions

DeletionDialog::DeletionDialog(
    const QString& folderName, const QStringList& localPaths, const QStringList& remotePaths, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "Confirm Deletions"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(i18np("Syncing \"%2\" will delete %1 item. Review the list before continuing.",
                                 "Syncing \"%2\" will delete %1 items. Review the list before continuing.",
                                 localPaths.size() + remotePaths.size(), folderName),
        this);
    intro->setObjectName(QStringLiteral("deletionIntro"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_local = new QListWidget(this);
    m_local->setObjectName(QStringLiteral("localDeletions"));
    m_local->addItems(localPaths);
    m_remote = new QListWidget(this);
    m_remote->setObjectName(QStringLiteral("remoteDeletions"));
    m_remote->addItems(remotePaths);
    if (!localPaths.isEmpty()) {
        layout->addWidget(
            new QLabel(i18n("Deleted from the local folder (moved to the trash when possible):"), this));
        layout->addWidget(m_local, 1);
    } else {
        m_local->hide();
    }
    if (!remotePaths.isEmpty()) {
        layout->addWidget(new QLabel(i18n("Deleted from Google Drive (moved to the Drive trash):"), this));
        layout->addWidget(m_remote, 1);
    } else {
        m_remote->hide();
    }

    m_buttons = new QDialogButtonBox(this);
    QPushButton* proceed = m_buttons->addButton(i18n("Delete and Sync"), QDialogButtonBox::AcceptRole);
    proceed->setObjectName(QStringLiteral("confirmDeletions"));
    proceed->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    QPushButton* cancel = m_buttons->addButton(i18n("Cancel Sync"), QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("cancelDeletions"));
    cancel->setDefault(true); // the safe choice is the default
    cancel->setAutoDefault(true);
    proceed->setAutoDefault(false);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
    resize(560, 420);
}

// ------------------------------------------------------------------ conflicts

QString ConflictDialog::describe(ConflictKind kind)
{
    switch (kind) {
    case ConflictKind::BothModified:
        return i18n("Changed locally and on Drive");
    case ConflictKind::LocalModifiedRemoteDeleted:
        return i18n("Changed locally, deleted on Drive");
    case ConflictKind::RemoteModifiedLocalDeleted:
        return i18n("Changed on Drive, deleted locally");
    case ConflictKind::None:
        break;
    }
    return {};
}

ConflictDialog::ConflictDialog(
    const QString& folderName, const QVector<ConflictInfo>& conflicts, QWidget* parent)
    : QDialog(parent)
    , m_conflicts(conflicts)
{
    setWindowTitle(i18nc("@title:window", "Resolve Conflicts"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(i18n("These files in \"%1\" changed on both sides since the last sync. Choose "
                                  "what to keep for each file.",
                                 folderName),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_table = new QTableWidget(conflicts.size(), 3, this);
    m_table->setObjectName(QStringLiteral("conflictTable"));
    m_table->setHorizontalHeaderLabels({i18n("File"), i18n("What happened"), i18n("Keep")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    for (int row = 0; row < conflicts.size(); ++row) {
        m_table->setItem(row, 0, new QTableWidgetItem(conflicts.at(row).path));
        m_table->setItem(row, 1, new QTableWidgetItem(describe(conflicts.at(row).kind)));
        auto* combo = new QComboBox(m_table);
        combo->addItem(i18n("Both versions"), int(ConflictChoice::KeepBoth));
        combo->addItem(i18n("Local version"), int(ConflictChoice::KeepLocal));
        combo->addItem(i18n("Drive version"), int(ConflictChoice::KeepRemote));
        combo->addItem(i18n("Decide later"), int(ConflictChoice::Skip));
        m_table->setCellWidget(row, 2, combo);
    }
    layout->addWidget(m_table, 1);

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(i18n("Continue Sync"), QDialogButtonBox::AcceptRole)
        ->setObjectName(QStringLiteral("applyConflicts"));
    buttons->addButton(i18n("Cancel Sync"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(680, 400);
}

QHash<QString, ConflictChoice> ConflictDialog::choices() const
{
    QHash<QString, ConflictChoice> out;
    for (int row = 0; row < m_conflicts.size(); ++row) {
        const auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, 2));
        out.insert(m_conflicts.at(row).path, ConflictChoice(combo->currentData().toInt()));
    }
    return out;
}

void ConflictDialog::setAll(ConflictChoice choice)
{
    for (int row = 0; row < m_conflicts.size(); ++row) {
        auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, 2));
        combo->setCurrentIndex(combo->findData(int(choice)));
    }
}

} // namespace gds
