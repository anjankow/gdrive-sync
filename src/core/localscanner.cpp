#include "localscanner.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>

namespace gds {

LocalTree scanLocalTree(const QString& root, QStringList* skipped, const std::atomic_bool* cancel)
{
    LocalTree tree;
    const QDir rootDir(root);
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancel && cancel->load())
            break;
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString rel = rootDir.relativeFilePath(info.absoluteFilePath());
        if (info.isSymLink() || (!info.isFile() && !info.isDir())) {
            if (skipped)
                skipped->append(QStringLiteral("%1: symbolic links and special files are not synced").arg(rel));
            continue;
        }
        if (info.fileName().endsWith(partialSuffix()))
            continue;
        LocalItem item;
        item.isDir = info.isDir();
        if (!item.isDir) {
            item.size = info.size();
            item.mtimeMs = info.lastModified().toMSecsSinceEpoch();
        }
        tree.insert(rel, item);
    }
    return tree;
}

QString fileMd5(const QString& path, const std::atomic_bool* cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Md5);
    QByteArray buffer(1 << 20, Qt::Uninitialized);
    while (true) {
        if (cancel && cancel->load())
            return {};
        const qint64 n = f.read(buffer.data(), buffer.size());
        if (n < 0)
            return {};
        if (n == 0)
            break;
        hash.addData(buffer.constData(), int(n));
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace gds
