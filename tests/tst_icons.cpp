// The application icon: every size installed into the hicolor theme is a
// square image with transparent corners, and the desktop file, the
// notifications and the window all use it.
#include <QDir>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QTest>

class TestIcons : public QObject {
    Q_OBJECT

    static QString data(const QString& path) { return QStringLiteral(GDS_SOURCE_DIR "/data/") + path; }
    static QString read(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

private Q_SLOTS:
    void everySizeIsASquareImageWithTransparentCorners()
    {
        const QString name = QStringLiteral("-apps-io.github.anjankow.gdrivesync.png");
        for (int size : {16, 22, 24, 32, 48, 64, 128, 256, 512}) {
            const QImage image(data(QStringLiteral("icons/%1%2").arg(size).arg(name)));
            QVERIFY2(!image.isNull(), qPrintable(QString::number(size)));
            QCOMPARE(image.size(), QSize(size, size));
            QVERIFY(image.hasAlphaChannel());
            // rounded corners: at 16 px the curve covers only part of the corner pixel
            QVERIFY(qAlpha(image.pixel(0, 0)) < 128);
            QVERIFY(qAlpha(image.pixel(size - 1, size - 1)) < 128);
            QCOMPARE(qAlpha(image.pixel(size / 2, size / 2)), 255);
        }
    }

    void desktopFileAndNotificationsUseTheIcon()
    {
        QVERIFY(read(data(QStringLiteral("io.github.anjankow.gdrivesync.desktop")))
                    .contains(QRegularExpression(QStringLiteral("^Icon=io\\.github\\.anjankow\\.gdrivesync$"),
                        QRegularExpression::MultilineOption)));
        QVERIFY(
            read(data(QStringLiteral("gdrive-sync.notifyrc")))
                .contains(QRegularExpression(QStringLiteral("^IconName=io\\.github\\.anjankow\\.gdrivesync$"),
                    QRegularExpression::MultilineOption)));
    }
};

QTEST_GUILESS_MAIN(TestIcons)
#include "tst_icons.moc"
