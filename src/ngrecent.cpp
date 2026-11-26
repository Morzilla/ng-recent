// SPDX-License-Identifier: LGPL-2.1-or-later
// ngrecent:/ — what happened lately to the user's files and folders, as a folder any KDE file view can show: Dolphin NG
// and Dialog NG list it in place of the desktop's "Recent Files". Each entry is the real file (its own URL), with the
// time of the event as its access time, so that "sorted by last access" is the order of the events, and with what
// happened as its comment ("Downloaded from kde.org").
#include "events.h"

#include <KIO/WorkerBase>

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrl>

#include <sys/stat.h>

class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.ngrecent" FILE "ngrecent.json")
};

class NgRecentWorker : public KIO::WorkerBase
{
public:
    NgRecentWorker(const QByteArray &pool, const QByteArray &app)
        : WorkerBase("ngrecent", pool, app)
    {
    }

    KIO::WorkerResult listDir(const QUrl &url) override
    {
        if (!url.path().isEmpty() && url.path() != QLatin1String("/")) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        }
        const QList<NgRecent::Event> all = NgRecent::events();
        int index = 0;
        for (const NgRecent::Event &event : all) {
            KIO::UDSEntry entry;
            if (fill(&entry, event, index++)) {
                listEntry(entry);
            }
        }
        return KIO::WorkerResult::pass();
    }

    KIO::WorkerResult stat(const QUrl &url) override
    {
        if (url.path().isEmpty() || url.path() == QLatin1String("/")) {
            KIO::UDSEntry entry;
            entry.reserve(5);
            entry.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
            entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, QLocale().language() == QLocale::Italian ? QStringLiteral("Recenti") : QStringLiteral("Recent"));
            entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
            entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
            entry.fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("document-open-recent"));
            statEntry(entry);
            return KIO::WorkerResult::pass();
        }
        // (an entry's own name, "12 name": applications are given the file's real URL and ask that instead)
        const QString name = url.fileName();
        const QList<NgRecent::Event> all = NgRecent::events();
        int index = 0;
        for (const NgRecent::Event &event : all) {
            KIO::UDSEntry entry;
            if (unique(event, index++) == name && fill(&entry, event, index - 1)) {
                statEntry(entry);
                return KIO::WorkerResult::pass();
            }
        }
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    }

    KIO::WorkerResult mimetype(const QUrl &url) override
    {
        if (url.path().isEmpty() || url.path() == QLatin1String("/")) {
            mimeType(QStringLiteral("inode/directory"));
            return KIO::WorkerResult::pass();
        }
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    }

private:
    static QString unique(const NgRecent::Event &event, int index)
    {
        // (two files of the same name in two folders are two entries)
        return QStringLiteral("%1 %2").arg(index, 3, 10, QLatin1Char('0')).arg(QFileInfo(event.path).fileName());
    }

    bool fill(KIO::UDSEntry *entry, const NgRecent::Event &event, int index)
    {
        struct stat info;
        const QByteArray path = QFile::encodeName(event.path);
        if (::stat(path.constData(), &info) != 0) {
            return false;
        }
        const QFileInfo file(event.path);
        const QUrl real = QUrl::fromLocalFile(event.path);
        entry->reserve(16);
        entry->fastInsert(KIO::UDSEntry::UDS_NAME, unique(event, index));
        entry->fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, file.fileName());
        entry->fastInsert(KIO::UDSEntry::UDS_URL, real.toString());
        entry->fastInsert(KIO::UDSEntry::UDS_TARGET_URL, real.toString());
        entry->fastInsert(KIO::UDSEntry::UDS_LOCAL_PATH, event.path);
        entry->fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, info.st_mode & S_IFMT);
        entry->fastInsert(KIO::UDSEntry::UDS_ACCESS, info.st_mode & 07777);
        entry->fastInsert(KIO::UDSEntry::UDS_SIZE, info.st_size);
        entry->fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, info.st_mtime);
        entry->fastInsert(KIO::UDSEntry::UDS_ACCESS_TIME, event.when / 1000); // when it happened: the order of the list
        if (file.birthTime().isValid()) {
            entry->fastInsert(KIO::UDSEntry::UDS_CREATION_TIME, file.birthTime().toSecsSinceEpoch());
        }
        entry->fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, S_ISDIR(info.st_mode) ? QStringLiteral("inode/directory") : m_types.mimeTypeForFile(event.path, QMimeDatabase::MatchExtension).name());
        entry->fastInsert(KIO::UDSEntry::UDS_COMMENT, NgRecent::describe(event));
        entry->fastInsert(KIO::UDSEntry::UDS_EXTRA, QString::number(int(event.kind)));
        if (!event.source.isEmpty()) {
            entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 1, event.source);
        }
        return true;
    }

    QMimeDatabase m_types;
};

extern "C" int Q_DECL_EXPORT kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kio_ngrecent"));
    if (argc != 4) {
        fprintf(stderr, "Usage: kio_ngrecent protocol domain-socket1 domain-socket2\n");
        return 1;
    }
    NgRecentWorker worker(argv[2], argv[3]);
    worker.dispatchLoop();
    return 0;
}

#include "ngrecent.moc"
