// SPDX-License-Identifier: LGPL-2.1-or-later
// ngrecent:/ — what happened lately to the user's files and folders, as a folder any KDE file view can show: Dolphin NG
// and Dialog NG list it in place of the desktop's "Recent Files". Each entry is the real file (its own URL), with the
// time of the event as its access time, so that "sorted by last access" is the order of the events, and with what
// happened as its comment ("Downloaded from kde.org"). An entry of this list carries the mark "ngrecent" in UDS_EXTRA + 7.
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
        // "/": the list; "/op/<id>": the files of one operation (a line of the list that opens like a folder)
        const quint64 operation = operationOf(url);
        if (!url.path().isEmpty() && url.path() != QLatin1String("/") && operation == 0) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        }
        KIO::UDSEntry self; // (the folder itself, as KIO expects of a listing)
        self.reserve(3);
        self.fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("."));
        self.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        self.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
        listEntry(self);
        const NgRecent::Listing listing = NgRecent::listing();
        if (operation != 0 && !listing.members.contains(operation)) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        }
        const QList<NgRecent::Event> all = operation != 0 ? listing.members.value(operation) : listing.top;
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
        const NgRecent::Listing listing = NgRecent::listing();
        if (const quint64 operation = operationOf(url)) {
            for (const NgRecent::Event &event : listing.top) {
                KIO::UDSEntry entry;
                if (event.kind == NgRecent::ManyChanged && event.operation == operation && fill(&entry, event, 0)) {
                    statEntry(entry);
                    return KIO::WorkerResult::pass();
                }
            }
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
        }
        // (an entry's own name, "12 name": applications are given the file's real URL and ask that instead)
        const QString name = url.fileName();
        const QList<NgRecent::Event> all = listing.top;
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
        if (url.path().isEmpty() || url.path() == QLatin1String("/") || operationOf(url) != 0) {
            mimeType(QStringLiteral("inode/directory"));
            return KIO::WorkerResult::pass();
        }
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.toDisplayString());
    }

private:
    static quint64 operationOf(const QUrl &url)
    {
        const QString path = url.path();
        return path.startsWith(QLatin1String("/op/")) ? path.mid(4).section(QLatin1Char('/'), 0, 0).toULongLong() : 0;
    }

    static QString unique(const NgRecent::Event &event, int index)
    {
        // (two files of the same name in two folders are two entries)
        return QStringLiteral("%1 %2").arg(index, 3, 10, QLatin1Char('0')).arg(QFileInfo(event.path).fileName());
    }

    bool fill(KIO::UDSEntry *entry, const NgRecent::Event &event, int index)
    {
        if (event.kind == NgRecent::ManyChanged && event.operation != 0) {
            // things done in one go: a folder of this list, named after the folder its files are in; it opens on them
            const QString name = QFileInfo(event.path).fileName();
            QUrl url(QStringLiteral("ngrecent:/op/%1").arg(event.operation));
            url.setQuery(QStringLiteral("title=") + QString::fromLatin1(QUrl::toPercentEncoding(name)));
            entry->reserve(14);
            entry->fastInsert(KIO::UDSEntry::UDS_NAME, QStringLiteral("op %1").arg(event.operation));
            entry->fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, name);
            entry->fastInsert(KIO::UDSEntry::UDS_URL, url.toString());
            entry->fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
            entry->fastInsert(KIO::UDSEntry::UDS_ACCESS, 0500);
            entry->fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, event.when / 1000);
            entry->fastInsert(KIO::UDSEntry::UDS_ACCESS_TIME, event.when / 1000);
            entry->fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
            entry->fastInsert(KIO::UDSEntry::UDS_ICON_NAME, QStringLiteral("folder-temp"));
            entry->fastInsert(KIO::UDSEntry::UDS_COMMENT, NgRecent::describe(event));
            // (the mark, the kind, the real folder its files are in, how many they are)
            entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 7, QStringLiteral("ngrecent"));
            entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 8, QString::number(int(event.kind)));
            entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 9, event.path);
            entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 10, QString::number(event.count));
            return true;
        }
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
        // (how a view knows an entry of this list from any other: KDE's own recentlyused:/ also fills UDS_EXTRA, with
        // the application's name. The mark, then the kind of event and the file it was made from)
        entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 7, QStringLiteral("ngrecent"));
        entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 8, QString::number(int(event.kind)));
        if (!event.source.isEmpty()) {
            entry->fastInsert(KIO::UDSEntry::UDS_EXTRA + 9, event.source);
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
