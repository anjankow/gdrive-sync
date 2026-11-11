// Unit tests of the three-way sync planner (pure logic, no I/O).
#include "planner.h"

#include <QTest>

using namespace gds;

namespace {
LocalItem lfile(qint64 size, qint64 mtime = 1000)
{
    LocalItem i;
    i.size = size;
    i.mtimeMs = mtime;
    return i;
}
LocalItem ldir()
{
    LocalItem i;
    i.isDir = true;
    return i;
}
RemoteItem rfile(const QString& id, qint64 size, const QString& md5)
{
    RemoteItem i;
    i.id = id;
    i.size = size;
    i.md5 = md5;
    return i;
}
RemoteItem rdir(const QString& id)
{
    RemoteItem i;
    i.id = id;
    i.isDir = true;
    return i;
}
StateEntry sfile(const QString& id, qint64 size, const QString& md5, qint64 mtime = 1000)
{
    StateEntry e;
    e.driveId = id;
    e.size = size;
    e.md5 = md5;
    e.localMtimeMs = mtime;
    return e;
}
StateEntry sdir(const QString& id)
{
    StateEntry e;
    e.isDir = true;
    e.driveId = id;
    return e;
}

QString describe(const SyncPlan& plan)
{
    QStringList out;
    for (const SyncAction& a : plan.actions) {
        static const char* names[] = {"rename", "mkdirL", "mkdirR", "download", "upload", "deleteL",
            "deleteR", "adopt", "forget", "conflict"};
        out.append(QLatin1String(names[int(a.type)]) + QLatin1Char(':') + a.path);
    }
    return out.join(QLatin1Char(' '));
}
}

class TestPlanner : public QObject {
    Q_OBJECT
    QHash<QString, QString> m_md5;
    Md5Provider provider()
    {
        return [this](const QString& p) { return m_md5.value(p); };
    }
    SyncPlan plan(const LocalTree& l, const RemoteTree& r, const StateMap& s, SyncDirection d)
    {
        return buildPlan(l, r, s, d, provider());
    }

private Q_SLOTS:
    void init() { m_md5.clear(); }

    void initialDownloadOfRemoteTree()
    {
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))},
            {QStringLiteral("sub"), rdir(QStringLiteral("2"))},
            {QStringLiteral("sub/b.txt"), rfile(QStringLiteral("3"), 20, QStringLiteral("m3"))}};
        const SyncPlan p = plan({}, r, {}, SyncDirection::TwoWay);
        QCOMPARE(describe(p), QStringLiteral("mkdirL:sub download:a.txt download:sub/b.txt"));
        QCOMPARE(p.transferBytes(), 30);
        QCOMPARE(p.transferCount(), 2);
        QVERIFY(!p.hasDeletions());
    }

    void newLocalFileIsUploadedExceptInDriveToLocal()
    {
        LocalTree l {{QStringLiteral("n.txt"), lfile(5)}, {QStringLiteral("d"), ldir()}};
        QCOMPARE(describe(plan(l, {}, {}, SyncDirection::TwoWay)), QStringLiteral("mkdirR:d upload:n.txt"));
        QCOMPARE(
            describe(plan(l, {}, {}, SyncDirection::LocalToDrive)), QStringLiteral("mkdirR:d upload:n.txt"));
        QCOMPARE(describe(plan(l, {}, {}, SyncDirection::DriveToLocal)), QString());
    }

    void newRemoteFileIsIgnoredInLocalToDrive()
    {
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        QCOMPARE(describe(plan({}, r, {}, SyncDirection::LocalToDrive)), QString());
        QCOMPARE(describe(plan({}, r, {}, SyncDirection::DriveToLocal)), QStringLiteral("download:a.txt"));
    }

    void unchangedFilesProduceNoActions()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(10)}};
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        for (auto d : {SyncDirection::TwoWay, SyncDirection::DriveToLocal, SyncDirection::LocalToDrive})
            QCOMPARE(describe(plan(l, r, s, d)), QString());
    }

    void touchedButIdenticalFileIsNotUploaded()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(10, 2000)}};
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        m_md5.insert(QStringLiteral("a.txt"), QStringLiteral("m1"));
        QCOMPARE(describe(plan(l, r, s, SyncDirection::TwoWay)), QStringLiteral("adopt:a.txt"));
    }

    void localChangeIsUploaded()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(12, 2000)}};
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        QCOMPARE(describe(plan(l, r, s, SyncDirection::TwoWay)), QStringLiteral("upload:a.txt"));
        QCOMPARE(describe(plan(l, r, s, SyncDirection::LocalToDrive)), QStringLiteral("upload:a.txt"));
        QCOMPARE(describe(plan(l, r, s, SyncDirection::DriveToLocal)), QString());
    }

    void remoteChangeIsDownloaded()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(10)}};
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 11, QStringLiteral("m2"))}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        QCOMPARE(describe(plan(l, r, s, SyncDirection::TwoWay)), QStringLiteral("download:a.txt"));
        QCOMPARE(describe(plan(l, r, s, SyncDirection::DriveToLocal)), QStringLiteral("download:a.txt"));
        QCOMPARE(describe(plan(l, r, s, SyncDirection::LocalToDrive)), QString());
    }

    void bothChangedIsConflictInTwoWayAndSourceWinsOtherwise()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(12, 2000)}};
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 11, QStringLiteral("m2"))}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        const SyncPlan p = plan(l, r, s, SyncDirection::TwoWay);
        QCOMPARE(describe(p), QStringLiteral("conflict:a.txt"));
        QCOMPARE(p.conflicts().first().conflict, ConflictKind::BothModified);
        QCOMPARE(describe(plan(l, r, s, SyncDirection::DriveToLocal)), QStringLiteral("download:a.txt"));
        QCOMPARE(describe(plan(l, r, s, SyncDirection::LocalToDrive)), QStringLiteral("upload:a.txt"));
    }

    void remoteDeletionPropagatesAsLocalDeletion()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(10)}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        const SyncPlan p = plan(l, {}, s, SyncDirection::TwoWay);
        QCOMPARE(describe(p), QStringLiteral("deleteL:a.txt"));
        QCOMPARE(p.localDeletions(), QStringList {QStringLiteral("a.txt")});
        QVERIFY(p.hasDeletions());
        QCOMPARE(describe(plan(l, {}, s, SyncDirection::DriveToLocal)), QStringLiteral("deleteL:a.txt"));
        // local is the source of truth: the file is restored on Drive instead
        QCOMPARE(describe(plan(l, {}, s, SyncDirection::LocalToDrive)), QStringLiteral("upload:a.txt"));
    }

    void localDeletionPropagatesAsRemoteDeletion()
    {
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        const SyncPlan p = plan({}, r, s, SyncDirection::TwoWay);
        QCOMPARE(describe(p), QStringLiteral("deleteR:a.txt"));
        QCOMPARE(p.remoteDeletions(), QStringList {QStringLiteral("a.txt")});
        QCOMPARE(describe(plan({}, r, s, SyncDirection::LocalToDrive)), QStringLiteral("deleteR:a.txt"));
        QCOMPARE(describe(plan({}, r, s, SyncDirection::DriveToLocal)), QStringLiteral("download:a.txt"));
    }

    void deletionAgainstModificationIsConflict()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(12, 2000)}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        SyncPlan p = plan(l, {}, s, SyncDirection::TwoWay);
        QCOMPARE(p.conflicts().first().conflict, ConflictKind::LocalModifiedRemoteDeleted);

        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 11, QStringLiteral("m2"))}};
        p = plan({}, r, s, SyncDirection::TwoWay);
        QCOMPARE(p.conflicts().first().conflict, ConflictKind::RemoteModifiedLocalDeleted);
    }

    void deletedDirectoryIsRemovedAfterItsFiles()
    {
        LocalTree l {{QStringLiteral("d"), ldir()}, {QStringLiteral("d/x.txt"), lfile(1)},
            {QStringLiteral("d/sub"), ldir()}, {QStringLiteral("d/sub/y.txt"), lfile(1)}};
        StateMap s {{QStringLiteral("d"), sdir(QStringLiteral("1"))},
            {QStringLiteral("d/x.txt"), sfile(QStringLiteral("2"), 1, QStringLiteral("m"))},
            {QStringLiteral("d/sub"), sdir(QStringLiteral("3"))},
            {QStringLiteral("d/sub/y.txt"), sfile(QStringLiteral("4"), 1, QStringLiteral("m"))}};
        const SyncPlan p = plan(l, {}, s, SyncDirection::TwoWay);
        QCOMPARE(describe(p), QStringLiteral("deleteL:d/sub/y.txt deleteL:d/x.txt deleteL:d/sub deleteL:d"));
        QCOMPARE(p.localDeletions(),
            (QStringList {QStringLiteral("d/sub/y.txt"), QStringLiteral("d/x.txt"), QStringLiteral("d/sub/"),
                QStringLiteral("d/")}));
    }

    void deletedDirectoryWithNewLocalFileIsKeptAndRecreated()
    {
        LocalTree l {{QStringLiteral("d"), ldir()}, {QStringLiteral("d/old.txt"), lfile(1)},
            {QStringLiteral("d/new.txt"), lfile(2)}};
        StateMap s {{QStringLiteral("d"), sdir(QStringLiteral("1"))},
            {QStringLiteral("d/old.txt"), sfile(QStringLiteral("2"), 1, QStringLiteral("m"))}};
        const SyncPlan p = plan(l, {}, s, SyncDirection::TwoWay);
        QCOMPARE(describe(p), QStringLiteral("mkdirR:d upload:d/new.txt deleteL:d/old.txt"));
    }

    void locallyDeletedDirectoryIsTrashedOnDrive()
    {
        RemoteTree r {{QStringLiteral("d"), rdir(QStringLiteral("1"))},
            {QStringLiteral("d/x.txt"), rfile(QStringLiteral("2"), 1, QStringLiteral("m"))}};
        StateMap s {{QStringLiteral("d"), sdir(QStringLiteral("1"))},
            {QStringLiteral("d/x.txt"), sfile(QStringLiteral("2"), 1, QStringLiteral("m"))}};
        QCOMPARE(
            describe(plan({}, r, s, SyncDirection::TwoWay)), QStringLiteral("deleteR:d/x.txt deleteR:d"));
    }

    void sameContentOnBothSidesWithoutStateIsAdopted()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(10)}};
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        m_md5.insert(QStringLiteral("a.txt"), QStringLiteral("m1"));
        QCOMPARE(describe(plan(l, r, {}, SyncDirection::TwoWay)), QStringLiteral("adopt:a.txt"));
        m_md5.insert(QStringLiteral("a.txt"), QStringLiteral("other"));
        QCOMPARE(describe(plan(l, r, {}, SyncDirection::TwoWay)), QStringLiteral("conflict:a.txt"));
    }

    void goneOnBothSidesIsForgotten()
    {
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        QCOMPARE(describe(plan({}, {}, s, SyncDirection::TwoWay)), QStringLiteral("forget:a.txt"));
    }

    void fileVersusFolderIsSkipped()
    {
        LocalTree l {{QStringLiteral("x"), ldir()}, {QStringLiteral("x/in.txt"), lfile(1)}};
        RemoteTree r {{QStringLiteral("x"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        const SyncPlan p = plan(l, r, {}, SyncDirection::TwoWay);
        QCOMPARE(describe(p), QString());
        QCOMPARE(p.skipped.size(), 1);
    }

    void unsetDirectionPlansNothing()
    {
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        QVERIFY(plan({}, r, {}, SyncDirection::Unset).actions.isEmpty());
    }

    void conflictChoicesAreApplied()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(12, 2000)}};
        RemoteTree r {{QStringLiteral("a.txt"), rfile(QStringLiteral("1"), 11, QStringLiteral("m2"))}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        const SyncPlan p = plan(l, r, s, SyncDirection::TwoWay);
        const QString suffix = QStringLiteral(" (conflict copy)");
        const QString path = QStringLiteral("a.txt");

        QCOMPARE(describe(applyConflictChoices(p, {{path, ConflictChoice::KeepLocal}}, l, r, suffix)),
            QStringLiteral("upload:a.txt"));
        QCOMPARE(describe(applyConflictChoices(p, {{path, ConflictChoice::KeepRemote}}, l, r, suffix)),
            QStringLiteral("download:a.txt"));
        const SyncPlan both = applyConflictChoices(p, {{path, ConflictChoice::KeepBoth}}, l, r, suffix);
        QCOMPARE(describe(both), QStringLiteral("rename:a.txt upload:a (conflict copy).txt download:a.txt"));
        QCOMPARE(both.transferBytes(), 23);
        const SyncPlan skipped = applyConflictChoices(p, {}, l, r, suffix);
        QVERIFY(skipped.actions.isEmpty());
        QCOMPARE(skipped.skipped.size(), 1);
    }

    void keepRemoteOfDeletedFileBecomesAVisibleDeletion()
    {
        LocalTree l {{QStringLiteral("a.txt"), lfile(12, 2000)}};
        StateMap s {{QStringLiteral("a.txt"), sfile(QStringLiteral("1"), 10, QStringLiteral("m1"))}};
        const SyncPlan p = plan(l, {}, s, SyncDirection::TwoWay);
        const SyncPlan resolved = applyConflictChoices(
            p, {{QStringLiteral("a.txt"), ConflictChoice::KeepRemote}}, l, {}, QString());
        QVERIFY(resolved.hasDeletions());
        QCOMPARE(resolved.localDeletions(), QStringList {QStringLiteral("a.txt")});
    }

    void unattendedPlanDropsDeletionsAndConflicts()
    {
        SyncPlan p;
        SyncAction a;
        a.type = ActionType::DeleteLocal;
        p.actions.append(a);
        a.type = ActionType::DeleteRemote;
        p.actions.append(a);
        a.type = ActionType::Conflict;
        p.actions.append(a);
        a.type = ActionType::Download;
        p.actions.append(a);
        int deletions = 0, conflicts = 0;
        const SyncPlan out = withoutDeletionsAndConflicts(p, &deletions, &conflicts);
        QCOMPARE(deletions, 2);
        QCOMPARE(conflicts, 1);
        QCOMPARE(out.actions.size(), 1);
        QVERIFY(!out.hasDeletions());
    }

    void conflictCopyNameAvoidsCollisions()
    {
        LocalTree l {{QStringLiteral("d/a (c).txt"), lfile(1)}};
        QCOMPARE(conflictCopyPath(QStringLiteral("d/a.txt"), QStringLiteral(" (c)"), l),
            QStringLiteral("d/a (c) 2.txt"));
        QCOMPARE(conflictCopyPath(QStringLiteral("noext"), QStringLiteral(" (c)"), {}),
            QStringLiteral("noext (c)"));
        QCOMPARE(conflictCopyPath(QStringLiteral(".hidden"), QStringLiteral(" (c)"), {}),
            QStringLiteral(".hidden (c)"));
    }
};

QTEST_GUILESS_MAIN(TestPlanner)
#include "tst_planner.moc"
