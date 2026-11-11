// Requirement: "The user can see a progress bar on sync and is informed how
// many bytes is synced and how many is remaining."
#include "format.h"
#include "testutil.h"

using namespace gds;
using namespace gdstest;

class TestProgress : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void bytesAreFormatted()
    {
        QCOMPARE(formatBytes(0), QStringLiteral("0 B"));
        QCOMPARE(formatBytes(1023), QStringLiteral("1023 B"));
        QCOMPARE(formatBytes(1024), QStringLiteral("1.0 KiB"));
        QCOMPARE(formatBytes(1536), QStringLiteral("1.5 KiB"));
        QCOMPARE(formatBytes(5 * 1024 * 1024), QStringLiteral("5.0 MiB"));
        QCOMPARE(formatBytes(3LL * 1024 * 1024 * 1024), QStringLiteral("3.0 GiB"));
    }

    void progressTextShowsSyncedAndRemainingBytes()
    {
        SyncProgress p;
        p.phase = QStringLiteral("transfer");
        p.bytesTotal = 10 * 1024 * 1024;
        p.bytesDone = 3 * 1024 * 1024;
        p.filesTotal = 5;
        p.filesDone = 2;
        QCOMPARE(p.bytesRemaining(), 7 * 1024 * 1024);
        QCOMPARE(formatProgress(p), QStringLiteral("3.0 MiB of 10.0 MiB synced, 7.0 MiB remaining (2 of 5 files)"));
        QCOMPARE(progressPermille(p), 300);
        p.phase = QStringLiteral("done");
        QCOMPARE(progressPermille(p), 1000);
    }

    void downloadReportsMonotonicByteProgress()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        f.server.addFile(QStringLiteral("a.bin"), docs, QByteArray(400000, 'a'));
        f.server.addFile(QStringLiteral("b.bin"), docs, QByteArray(250000, 'b'));
        f.server.addFile(QStringLiteral("c.bin"), docs, QByteArray(1000, 'c'));
        f.server.setDownloadChunkDelayMs(15); // slow enough to observe intermediate states
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.bytesTransferred, 651000);

        const qint64 total = 651000;
        QVector<SyncProgress> transfer;
        for (const SyncProgress& p : qAsConst(f.progressEvents)) {
            if (p.phase != QLatin1String("scan"))
                transfer.append(p);
        }
        QVERIFY(f.progressEvents.first().phase == QLatin1String("scan"));
        QVERIFY2(transfer.size() >= 5, "expected intermediate progress reports");
        qint64 previous = -1;
        bool sawPartial = false;
        for (const SyncProgress& p : qAsConst(transfer)) {
            QCOMPARE(p.folderId, id);
            QCOMPARE(p.bytesTotal, total);
            QCOMPARE(p.filesTotal, 3);
            QVERIFY(p.bytesDone >= previous); // never goes backwards
            QVERIFY(p.bytesDone <= total);
            QCOMPARE(p.bytesRemaining(), total - p.bytesDone);
            if (p.bytesDone > 0 && p.bytesDone < total)
                sawPartial = true;
            previous = p.bytesDone;
        }
        QVERIFY(sawPartial);
        QCOMPARE(transfer.first().bytesDone, 0);
        QCOMPARE(transfer.last().phase, QStringLiteral("done"));
        QCOMPARE(transfer.last().bytesDone, total);
        QCOMPARE(transfer.last().bytesRemaining(), 0);
        QCOMPARE(transfer.last().filesDone, 3);
    }

    void uploadReportsByteProgress()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QVERIFY(writeFile(f.localDir(QStringLiteral("Docs")) + QStringLiteral("/up.bin"), QByteArray(900000, 'u')));
        const SyncResult r = f.sync(id);
        QCOMPARE(r.status, SyncResult::Success);
        QCOMPARE(r.uploaded, 1);
        const SyncProgress last = f.progressEvents.last();
        QCOMPARE(last.bytesTotal, 900000);
        QCOMPARE(last.bytesDone, 900000);
        QCOMPARE(last.bytesRemaining(), 0);
    }

    void failedTransferStillCompletesTheBar()
    {
        Fixture f;
        QVERIFY(f.startLoggedIn());
        const QString docs = f.server.addFolder(QStringLiteral("Docs"));
        const QString bad = f.server.addFile(QStringLiteral("bad.bin"), docs, QByteArray(5000, 'x'));
        f.server.addFile(QStringLiteral("good.bin"), docs, QByteArray(7000, 'y'));
        f.server.failNext(QStringLiteral("/drive/v3/files/") + bad, 404, 1);
        const QString id = f.mount(docs, QStringLiteral("Docs"));
        QCOMPARE(f.sync(id).status, SyncResult::Failed);
        QCOMPARE(f.progressEvents.last().bytesDone, f.progressEvents.last().bytesTotal);
    }
};

QTEST_GUILESS_MAIN(TestProgress)
#include "tst_progress.moc"
