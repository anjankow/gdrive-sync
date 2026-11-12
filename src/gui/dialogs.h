#pragma once

#include "appcore.h"

#include <QDialog>
#include <QHash>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QRadioButton;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace gds {

/// OAuth client of the user's own Google Cloud project.
class CredentialsDialog : public QDialog {
    Q_OBJECT
public:
    CredentialsDialog(const QString& clientId, const QString& clientSecret, QWidget* parent = nullptr);
    QString clientId() const;
    QString clientSecret() const;

private:
    QLineEdit* m_id;
    QLineEdit* m_secret;
};

/// Option to run the sync automatically (systemd user timer).
class AutoSyncDialog : public QDialog {
    Q_OBJECT
public:
    AutoSyncDialog(bool enabled, int intervalMinutes, QWidget* parent = nullptr);
    bool isEnabledChecked() const;
    int intervalMinutes() const;

private:
    QCheckBox* m_enabled;
    QSpinBox* m_interval;
};

/// Asked once after sign-in; valid until sign-out.
class DirectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit DirectionDialog(QWidget* parent = nullptr);
    SyncDirection direction() const;
    void setDirection(SyncDirection direction);

private:
    QRadioButton* m_twoWay;
    QRadioButton* m_driveToLocal;
    QRadioButton* m_localToDrive;
};

/// Choose a Drive folder (loaded lazily, in the background) and an empty
/// local destination.
class AddFolderDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddFolderDialog(AppCore* core, QWidget* parent = nullptr);

    QString driveFolderId() const;
    QString driveFolderName() const;
    QString localPath() const;
    void setLocalPath(const QString& path);

    QTreeWidget* tree() const { return m_tree; }
    QLabel* validationLabel() const { return m_validation; }
    QDialogButtonBox* buttons() const { return m_buttons; }

private:
    void loadChildren(QTreeWidgetItem* item);
    void onListed(const QString& parentId, const QVector<gds::DriveItem>& folders, const QString& error);
    void validate();
    void browse();

    AppCore* m_core;
    QTreeWidget* m_tree;
    QLineEdit* m_path;
    QLabel* m_validation;
    QDialogButtonBox* m_buttons;
    QHash<QString, QTreeWidgetItem*> m_items;
};

/// Lists every file that the sync is about to delete. Nothing is deleted
/// unless the user explicitly confirms; closing the dialog cancels the sync.
class DeletionDialog : public QDialog {
    Q_OBJECT
public:
    DeletionDialog(const QString& folderName, const QStringList& localPaths, const QStringList& remotePaths,
        QWidget* parent = nullptr);
    QListWidget* localList() const { return m_local; }
    QListWidget* remoteList() const { return m_remote; }
    QDialogButtonBox* buttons() const { return m_buttons; }

private:
    QListWidget* m_local;
    QListWidget* m_remote;
    QDialogButtonBox* m_buttons;
};

/// One row per conflicting file with the choice what to keep.
class ConflictDialog : public QDialog {
    Q_OBJECT
public:
    ConflictDialog(
        const QString& folderName, const QVector<gds::ConflictInfo>& conflicts, QWidget* parent = nullptr);
    QHash<QString, ConflictChoice> choices() const;
    void setAll(ConflictChoice choice);
    QTableWidget* table() const { return m_table; }

    static QString describe(ConflictKind kind);

private:
    QTableWidget* m_table;
    QVector<ConflictInfo> m_conflicts;
};

} // namespace gds
