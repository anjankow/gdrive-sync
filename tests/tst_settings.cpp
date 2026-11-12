// Requirement: "UI should be always responsive". Settings are written to disk
// by a background thread (the commit waits for the disk); these tests check
// that nothing is lost or reordered on the way.
#include "configwriter.h"

#include <KConfigGroup>

#include <QTemporaryDir>
#include <QTest>

using namespace gds;

class TestSettings : public QObject {
    Q_OBJECT

    static KSharedConfig::Ptr reopen(const QString& file)
    {
        return KSharedConfig::openConfig(file, KConfig::SimpleConfig);
    }

private Q_SLOTS:
    void savedSettingsReachTheDiskUnchanged()
    {
        QTemporaryDir tmp;
        const QString file = tmp.path() + QStringLiteral("/rc");
        {
            KSharedConfig::Ptr config = reopen(file);
            ConfigWriter writer(config);
            KConfigGroup account = config->group("Account");
            account.writeEntry("email", QStringLiteral("a@example.com"));
            account.writeEntry("direction", 2);
            KConfigGroup folders = config->group("Folders");
            folders.writeEntry("ids", QStringList {QStringLiteral("f1"), QStringLiteral("with,comma")});
            KConfigGroup folder = folders.group("f1");
            folder.writeEntry("localPath", QStringLiteral("/home/u/Drive Docs/ä\\x"));
            folder.writeEntry("note", QStringLiteral("line1\nline2"));
            writer.save();
            QVERIFY(!config->isDirty()); // the in-memory object never writes the file itself
            writer.flush();
        }
        KConfig disk(file, KConfig::SimpleConfig);
        QCOMPARE(disk.group("Account").readEntry("email", QString()), QStringLiteral("a@example.com"));
        QCOMPARE(disk.group("Account").readEntry("direction", 0), 2);
        QCOMPARE(disk.group("Folders").readEntry("ids", QStringList()),
            (QStringList {QStringLiteral("f1"), QStringLiteral("with,comma")}));
        const KConfigGroup folder = disk.group("Folders").group("f1");
        QCOMPARE(folder.readEntry("localPath", QString()), QStringLiteral("/home/u/Drive Docs/ä\\x"));
        QCOMPARE(folder.readEntry("note", QString()), QStringLiteral("line1\nline2"));
    }

    void removedEntriesAndGroupsAreRemovedOnDisk()
    {
        QTemporaryDir tmp;
        const QString file = tmp.path() + QStringLiteral("/rc");
        KSharedConfig::Ptr config = reopen(file);
        ConfigWriter writer(config);
        config->group("Account").writeEntry("email", QStringLiteral("a@example.com"));
        config->group("Folders").group("f1").writeEntry("localPath", QStringLiteral("/x"));
        config->group("Logging").writeEntry("directory", QStringLiteral("/logs"));
        writer.save();
        config->deleteGroup("Account");
        config->group("Folders").deleteGroup("f1");
        config->group("Logging").deleteEntry("directory");
        writer.save();
        writer.flush();
        KConfig disk(file, KConfig::SimpleConfig);
        QVERIFY(!disk.hasGroup("Account"));
        QVERIFY(!disk.group("Folders").hasGroup("f1"));
        QVERIFY(!disk.group("Logging").hasKey("directory"));
    }

    void theNewestOfManySavesWins()
    {
        QTemporaryDir tmp;
        const QString file = tmp.path() + QStringLiteral("/rc");
        KSharedConfig::Ptr config = reopen(file);
        ConfigWriter writer(config);
        for (int i = 0; i <= 500; ++i) {
            config->group("Account").writeEntry("direction", i);
            writer.save();
        }
        writer.flush();
        QCOMPARE(KConfig(file, KConfig::SimpleConfig).group("Account").readEntry("direction", -1), 500);
    }

    void destroyingTheWriterWritesWhatIsPending()
    {
        QTemporaryDir tmp;
        const QString file = tmp.path() + QStringLiteral("/rc");
        {
            KSharedConfig::Ptr config = reopen(file);
            ConfigWriter writer(config);
            config->group("Account").writeEntry("email", QStringLiteral("late@example.com"));
            writer.save();
        }
        QCOMPARE(KConfig(file, KConfig::SimpleConfig).group("Account").readEntry("email", QString()),
            QStringLiteral("late@example.com"));
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
