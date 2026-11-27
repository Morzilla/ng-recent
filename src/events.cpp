// SPDX-License-Identifier: LGPL-2.1-or-later
#include "events.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>
#include <sys/xattr.h>

namespace NgRecent
{
static QString tr2(const char *english, const char *italian)
{
    return QLocale().language() == QLocale::Italian ? QString::fromUtf8(italian) : QString::fromUtf8(english);
}

static QString attribute(const QString &path, const char *name)
{
    char value[1024];
    const ssize_t size = getxattr(QFile::encodeName(path).constData(), name, value, sizeof(value));
    return size > 0 ? QString::fromUtf8(value, int(size)) : QString();
}

QString journalPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/ng-recent/journal.tsv");
}

void note(const QString &path, Kind kind, const QString &detail, const QString &source)
{
    const QString file = journalPath();
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile journal(file);
    // (kept short: past 400 KiB the older half goes)
    if (journal.size() > 400 * 1024 && journal.open(QIODevice::ReadOnly)) {
        const QByteArray all = journal.readAll();
        journal.close();
        const int cut = all.indexOf('\n', all.size() / 2);
        if (cut > 0 && journal.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            journal.write(all.mid(cut + 1));
            journal.close();
        }
    }
    if (!journal.open(QIODevice::WriteOnly | QIODevice::Append)) {
        return;
    }
    auto clean = [](QString text) {
        return text.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' '));
    };
    const QStringList fields{QString::number(QDateTime::currentMSecsSinceEpoch()), QString::number(int(kind)), clean(path), clean(detail), clean(source)};
    journal.write(fields.join(QLatin1Char('\t')).toUtf8() + '\n');
}

namespace
{
struct Collector {
    qint64 since = 0;
    QHash<QString, Event> newest;

    void add(const Event &event)
    {
        if (event.when < since || event.path.isEmpty()) {
            return;
        }
        auto it = newest.find(event.path);
        if (it == newest.end()) {
            newest.insert(event.path, event);
            return;
        }
        // one line for each file: its latest event. But "opened" or "modified" a moment after it arrived says less
        // than how it arrived (a download is opened at once; a new file is written to for a while), and what a
        // program said it did (a journal line, a quick action) says more than what the dates of the file suggest
        const bool plain = event.kind == Opened || event.kind == Modified || event.kind == Created;
        const bool told = it->kind == Downloaded || it->kind == Made || it->kind == Added;
        if (event.when >= it->when) {
            if (plain && (told || it->kind == Created) && event.when - it->when < 180000) {
                return;
            }
            const Event before = *it;
            *it = event;
            if (it->folder != before.folder) {
                it->folder = before.folder || event.folder;
            }
        } else if (!plain && (it->kind == Opened || it->kind == Modified || it->kind == Created) && it->when - event.when < 180000) {
            const qint64 when = it->when;
            *it = event;
            it->when = when;
        }
    }

    // the folders people keep their things in, two levels deep: what is new in them, by the dates of the files
    void scan(const QString &folder, int depth, int *budget)
    {
        const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : entries) {
            if (--*budget < 0) {
                return;
            }
            const QString name = info.fileName();
            if (name.endsWith(QLatin1String(".part")) || name.endsWith(QLatin1String(".crdownload")) || name.endsWith(QLatin1Char('~'))) {
                continue;
            }
            const qint64 born = info.birthTime().isValid() ? info.birthTime().toMSecsSinceEpoch() : 0;
            const qint64 changed = info.lastModified().toMSecsSinceEpoch();
            Event event;
            event.path = info.absoluteFilePath();
            event.folder = info.isDir();
            if (event.folder) {
                // (a folder's own date moves with every file put in it: only its making counts)
                if (born >= since) {
                    event.kind = Created;
                    event.when = born;
                    add(event);
                }
                if (depth > 1 && !info.isSymLink()) {
                    scan(event.path, depth - 1, budget);
                }
                continue;
            }
            event.when = qMax(born, changed);
            if (event.when < since) {
                continue;
            }
            event.kind = born > changed + 5000 ? Added : (born > 0 && changed > born + 5000) ? Modified : Created;
            const QString action = attribute(event.path, "user.dolphin-ng.made-by");
            const QString origin = attribute(event.path, "user.xdg.origin.url");
            if (!action.isEmpty() && event.kind != Modified) {
                event.kind = Made;
                event.detail = action;
                event.source = attribute(event.path, "user.dolphin-ng.made-from");
            } else if (!origin.isEmpty() && event.kind != Modified) {
                QString host = QUrl(origin).host();
                if (host.startsWith(QLatin1String("www."))) {
                    host = host.mid(4);
                }
                if (!host.isEmpty()) {
                    event.kind = Downloaded;
                    event.detail = host;
                }
            }
            add(event);
        }
    }

    // the desktop's recent documents: what applications opened
    void opened()
    {
        QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel"));
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement || xml.name() != QLatin1String("bookmark")) {
                continue;
            }
            const QUrl url(xml.attributes().value(QLatin1String("href")).toString());
            if (!url.isLocalFile()) {
                continue;
            }
            QDateTime when = QDateTime::fromString(xml.attributes().value(QLatin1String("visited")).toString(), Qt::ISODate);
            if (!when.isValid()) {
                when = QDateTime::fromString(xml.attributes().value(QLatin1String("modified")).toString(), Qt::ISODate);
            }
            if (!when.isValid() || when.toMSecsSinceEpoch() < since) {
                continue;
            }
            const QFileInfo info(url.toLocalFile());
            if (!info.exists() || info.fileName().startsWith(QLatin1Char('.'))) {
                continue;
            }
            Event event;
            event.path = info.absoluteFilePath();
            event.folder = info.isDir();
            event.kind = Opened;
            event.when = when.toMSecsSinceEpoch();
            add(event);
        }
    }

    // what programs wrote down themselves: time, kind, path, detail, source
    void journal()
    {
        QFile file(journalPath());
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList f = line.split(QLatin1Char('\t'));
            if (f.size() < 3) {
                continue;
            }
            const QFileInfo info(f.at(2));
            const int kind = f.at(1).toInt();
            if (!info.exists() || kind < Created || kind > Opened) {
                continue;
            }
            Event event;
            event.when = f.at(0).toLongLong();
            event.kind = Kind(kind);
            event.path = info.absoluteFilePath();
            event.folder = info.isDir();
            event.detail = f.value(3);
            event.source = f.value(4);
            add(event);
        }
    }
};
}

QList<Event> events(int days, int limit)
{
    Collector collector;
    collector.since = QDateTime::currentMSecsSinceEpoch() - qint64(days) * 86400000;
    QSet<QString> roots;
    for (const QStandardPaths::StandardLocation place : {QStandardPaths::DownloadLocation, QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation,
                                                         QStandardPaths::PicturesLocation, QStandardPaths::MoviesLocation, QStandardPaths::MusicLocation}) {
        const QString folder = QStandardPaths::writableLocation(place);
        if (!folder.isEmpty() && QDir(folder).exists() && folder != QDir::homePath()) {
            roots.insert(QDir(folder).absolutePath());
        }
    }
    int budget = 20000; // (entries looked at: a list for a dialog must come at once)
    for (const QString &root : std::as_const(roots)) {
        collector.scan(root, 2, &budget);
    }
    collector.scan(QDir::homePath(), 1, &budget); // (the home itself: its own files and new folders, not what is in them)
    collector.opened();
    collector.journal();
    QList<Event> all = collector.newest.values();
    std::sort(all.begin(), all.end(), [](const Event &a, const Event &b) {
        return a.when > b.when;
    });
    return all.mid(0, limit);
}

QString describe(const Event &event)
{
    const QString from = event.source.isEmpty() ? QString() : QFileInfo(event.source).fileName();
    switch (event.kind) {
    case Created:
        return event.folder ? tr2("New folder", "Nuova cartella") : tr2("New", "Nuovo");
    case Added:
        return from.isEmpty() ? tr2("Added", "Aggiunto") : tr2("Added, from ", "Aggiunto, da ") + from;
    case Modified:
        return tr2("Modified", "Modificato");
    case Downloaded:
        return event.detail.isEmpty() ? tr2("Downloaded", "Scaricato") : tr2("Downloaded from ", "Scaricato da ") + event.detail;
    case Made:
        return (event.detail.isEmpty() ? tr2("Made", "Prodotto") : event.detail) + (from.isEmpty() ? QString() : tr2(", from ", ", da ") + from);
    case Opened:
        return tr2("Opened", "Aperto");
    }
    return QString();
}
}
