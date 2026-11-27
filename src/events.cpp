// SPDX-License-Identifier: LGPL-2.1-or-later
#include "events.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
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

void note(const QString &path, Kind kind, const QString &detail, const QString &source, const QString &by)
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
    const QStringList fields{QString::number(QDateTime::currentMSecsSinceEpoch()), QString::number(int(kind)), clean(path), clean(detail), clean(source), clean(by)};
    journal.write(fields.join(QLatin1Char('\t')).toUtf8() + '\n');
}

namespace
{
// The applications of the desktop, to give a program its name ("gimp-3.0" is "GIMP") and to tell a browser:
// by the id of the desktop entry and by the program it starts
struct Application {
    QString name;
    bool browser = false;
    bool hidden = false; // not shown in the menus
    bool own = false; // the entry named after the program
    QString tool; // no application: the bare name of the program
};
struct Applications {
    QHash<QString, Application> byId;
    QHash<QString, Application> byProgram;

    Applications()
    {
        const QString language = QLocale().name(); // it_IT
        const QStringList wanted{QStringLiteral("Name[") + language + QLatin1Char(']'), QStringLiteral("Name[") + language.section(QLatin1Char('_'), 0, 0) + QLatin1Char(']'),
                                 QStringLiteral("Name")};
        const QStringList folders = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        for (const QString &folder : folders) {
            QDirIterator it(folder, {QStringLiteral("*.desktop")}, QDir::Files);
            while (it.hasNext()) {
                QFile file(it.next());
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                QHash<QString, QString> keys;
                bool inEntry = false;
                while (!file.atEnd()) {
                    const QString line = QString::fromUtf8(file.readLine()).trimmed();
                    if (line.startsWith(QLatin1Char('['))) {
                        inEntry = line == QLatin1String("[Desktop Entry]");
                        continue;
                    }
                    const int equals = line.indexOf(QLatin1Char('='));
                    if (inEntry && equals > 0) {
                        keys.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
                    }
                }
                Application application;
                for (const QString &key : wanted) {
                    if (application.name.isEmpty()) {
                        application.name = keys.value(key);
                    }
                }
                if (application.name.isEmpty() || keys.value(QStringLiteral("Type"), QStringLiteral("Application")) != QLatin1String("Application")) {
                    continue;
                }
                application.browser = keys.value(QStringLiteral("Categories")).split(QLatin1Char(';')).contains(QLatin1String("WebBrowser"));
                const QString id = it.fileInfo().completeBaseName().toLower();
                if (!byId.contains(id)) {
                    byId.insert(id, application); // (the user's own entries come first in the list of folders)
                }
                // the program: the first word of Exec that is not "env" or a variable
                const QStringList words = keys.value(QStringLiteral("Exec")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                for (const QString &word : words) {
                    if (word == QLatin1String("env") || (word.contains(QLatin1Char('=')) && !word.startsWith(QLatin1Char('/')))) {
                        continue;
                    }
                    const QString program = word.section(QLatin1Char('/'), -1);
                    // (several entries may start one program: the program's own entry is preferred, "org.kde.ark" for
                    // "ark", then one that is shown in the menus; "Quick Extract" also starts ark)
                    application.hidden = keys.value(QStringLiteral("NoDisplay")) == QLatin1String("true");
                    application.own = id == program.toLower() || id.endsWith(QLatin1Char('.') + program.toLower());
                    const auto known = byProgram.constFind(program);
                    const bool better = known == byProgram.cend() || (application.own && !known->own) || (!known->own && known->hidden && !application.hidden);
                    if (!program.isEmpty() && !program.startsWith(QLatin1Char('%')) && better) {
                        byProgram.insert(program, application);
                    }
                    break;
                }
            }
        }
    }

    Application find(const QString &id, const QString &program) const
    {
        if (!id.isEmpty()) {
            const auto it = byId.constFind(id.toLower());
            if (it != byId.cend()) {
                return *it;
            }
            // (a Snap names its entries <snap>_<application>)
            const auto snap = byId.constFind(id.toLower() + QLatin1Char('_') + id.toLower());
            if (snap != byId.cend()) {
                return *snap;
            }
        }
        // tools that move files about for someone else say nothing about who did it; file managers say it better
        // themselves (Dolphin NG's own notes)
        static const QSet<QString> nobody{QStringLiteral("sh"),    QStringLiteral("bash"),  QStringLiteral("zsh"),   QStringLiteral("fish"),    QStringLiteral("dash"),
                                          QStringLiteral("cp"),    QStringLiteral("mv"),    QStringLiteral("touch"), QStringLiteral("mkdir"),   QStringLiteral("cat"),
                                          QStringLiteral("tee"),   QStringLiteral("dd"),    QStringLiteral("ln"),    QStringLiteral("install"), QStringLiteral("kioworker"),
                                          QStringLiteral("dolphin"), QStringLiteral("dolphin-ng"), QStringLiteral("ng-job")};
        if (program.isEmpty() || nobody.contains(program)) {
            return {};
        }
        // (a program that is no application of the desktop, a compiler or a tool: its name is kept apart, for the
        // one line of a bulk change; "modified with cc1plus" under a file tells nobody anything)
        const auto it = byProgram.constFind(program);
        return it != byProgram.cend() ? *it : Application{QString(), false, false, false, program};
    }
};

const Applications &desktopApplications()
{
    static const Applications applications;
    return applications;
}

struct Operation {
    QString folder;
    qint64 first = 0;
    qint64 last = 0;
    int count = 0;
    uint kinds = 0;
    QString by;
    QString tool; // the bare name of the program, when it is no application
};

struct Collector {
    qint64 since = 0;
    QHash<QString, Event> newest;
    QHash<quint64, Operation> operations;
    bool showOpened = false;

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
        // (two sources about one moment: who did it, and where a download came from, are kept from either)
        const bool together = qAbs(event.when - it->when) < 180000;
        const Event before = *it;
        if (event.when >= it->when) {
            if (plain && (told || it->kind == Created) && together) {
                if (it->by.isEmpty()) {
                    it->by = event.by;
                }
                if (it->operation == 0) {
                    it->operation = event.operation;
                }
                return;
            }
            *it = event;
            if (it->folder != before.folder) {
                it->folder = before.folder || event.folder;
            }
        } else if (!plain && (it->kind == Opened || it->kind == Modified || it->kind == Created) && together) {
            const qint64 when = it->when;
            *it = event;
            it->when = when;
        } else {
            if (together && it->by.isEmpty()) {
                it->by = event.by;
            }
            if (it->operation == 0) {
                it->operation = event.operation;
            }
            return;
        }
        if (it->operation == 0) {
            it->operation = before.operation; // (what the service knows of the file stays, whichever line wins)
        }
        if (together) {
            if (it->by.isEmpty()) {
                it->by = before.by;
            }
            if (it->kind == before.kind && it->detail.isEmpty()) {
                it->detail = before.detail;
            }
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

    // the ng-fsevents service, where it runs: the latest that happened to each file of the home, anywhere in it,
    // and which program did it. (path, when, kinds, program, application, count); kinds: 1 created, 2 modified,
    // 4 renamed or moved to where it is, 8 a folder, 16 a folder standing for many files changed at once
    void service()
    {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.ng.FsEvents1"), QStringLiteral("/org/ng/FsEvents1"), QStringLiteral("org.ng.FsEvents1"),
                                                           QStringLiteral("Events"));
        call << qlonglong(since) << uint(2000);
        const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 800);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return; // (not installed, or not running: the list is made without it)
        }
        const QDBusArgument list = reply.arguments().constFirst().value<QDBusArgument>();
        // (the last field was a count in the service's first version, and is the operation now)
        const bool withOperations = reply.signature() == QLatin1String("a(sxusst)");
        if (list.currentType() != QDBusArgument::ArrayType || (!withOperations && reply.signature() != QLatin1String("a(sxussu)"))) {
            return;
        }
        const Applications &applications = desktopApplications();
        list.beginArray();
        while (!list.atEnd()) {
            QString path, program, application;
            qlonglong when = 0;
            uint kinds = 0, count = 0;
            qulonglong operation = 0;
            list.beginStructure();
            list >> path >> when >> kinds >> program >> application;
            if (withOperations) {
                list >> operation;
            } else {
                list >> count;
            }
            list.endStructure();
            const QFileInfo info(path);
            if (!info.exists() || (kinds & 16)) {
                continue;
            }
            const Application app = applications.find(application, program);
            Event event;
            event.path = info.absoluteFilePath();
            event.when = when;
            event.folder = info.isDir();
            event.by = app.name;
            event.operation = operation;
            event.automatic = kinds & 32;
            if (event.automatic && event.by.isEmpty()) {
                event.by = app.tool; // (what works on its own is named, application or not: "onedrive")
            }
            if (kinds & 1) {
                // a new file made by a browser is a download (where from, when the browser wrote it on the file)
                event.kind = app.browser && !event.folder ? Downloaded : Created;
                if (event.kind == Downloaded) {
                    QString host = QUrl(attribute(event.path, "user.xdg.origin.url")).host();
                    event.detail = host.startsWith(QLatin1String("www.")) ? host.mid(4) : host;
                }
            } else if (kinds & 4) {
                event.kind = Added;
            } else {
                event.kind = Modified;
            }
            add(event);
        }
        list.endArray();
        if (!withOperations) {
            return;
        }
        // the things done in one go: (id, folder, first, last, how many files, kinds, program, application)
        QDBusMessage ask = QDBusMessage::createMethodCall(QStringLiteral("org.ng.FsEvents1"), QStringLiteral("/org/ng/FsEvents1"), QStringLiteral("org.ng.FsEvents1"),
                                                          QStringLiteral("Operations"));
        ask << qlonglong(since) << uint(2) << uint(2000);
        const QDBusMessage answer = QDBusConnection::systemBus().call(ask, QDBus::Block, 800);
        if (answer.type() != QDBusMessage::ReplyMessage || answer.signature() != QLatin1String("a(tsxxuuss)")) {
            return;
        }
        const QDBusArgument all = answer.arguments().constFirst().value<QDBusArgument>();
        all.beginArray();
        while (!all.atEnd()) {
            qulonglong id = 0;
            qlonglong first = 0, last = 0;
            uint count = 0, kinds = 0;
            QString folder, program, application;
            all.beginStructure();
            all >> id >> folder >> first >> last >> count >> kinds >> program >> application;
            all.endStructure();
            const Application app = applications.find(application, program);
            operations.insert(id, {folder, first, last, int(count), kinds, app.name.isEmpty() ? app.tool : app.name, app.tool});
        }
        all.endArray();
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
            if (!info.exists() || kind < Created || kind > ManyChanged) {
                continue;
            }
            Event event;
            event.when = f.at(0).toLongLong();
            event.kind = Kind(kind);
            event.path = info.absoluteFilePath();
            event.folder = info.isDir();
            if (event.kind == Opened && !showOpened) {
                continue;
            }
            event.detail = f.value(3);
            event.source = f.value(4);
            event.by = f.value(5);
            add(event);
        }
    }
};
}

QString applicationOf(qint64 pid)
{
    // the program: the first argument the process was started with, without its folder
    QString program;
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (cmdline.open(QIODevice::ReadOnly)) {
        QByteArray first = cmdline.read(4096);
        first.truncate(qMax<qsizetype>(0, first.indexOf('\0')));
        program = QFile::decodeName(first).section(QLatin1Char(' '), 0, 0).section(QLatin1Char('/'), -1);
    }
    // the application: from the name of its systemd scope or service (app-org.kde.kate-1a2b.scope,
    // app-org.kde.kate@1a2b.service, app-flatpak-org.gimp.GIMP-123.scope, snap.firefox.firefox-<uuid>.scope)
    QString id;
    QFile cgroup(QStringLiteral("/proc/%1/cgroup").arg(pid));
    if (cgroup.open(QIODevice::ReadOnly)) {
        const QString unit = QString::fromUtf8(cgroup.read(4096)).trimmed().section(QLatin1Char('/'), -1);
        static const QRegularExpression app(QStringLiteral("^app-(?:flatpak-|gnome-)?(.+?)(?:-[0-9a-f]+|@[^.]*)?\\.(?:scope|service)$"));
        static const QRegularExpression snap(QStringLiteral("^snap\\.([^.]+)\\."));
        if (const auto match = app.match(unit); match.hasMatch()) {
            id = match.captured(1);
        } else if (const auto snapMatch = snap.match(unit); snapMatch.hasMatch()) {
            id = snapMatch.captured(1);
        }
    }
    return desktopApplications().find(id, program).name;
}

static void collect(Collector &collector, int days)
{
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
    collector.service();
    // (what was only opened is left out unless asked for: the list is about what was made and what changed, and
    // every picture looked at would be a line. ~/.config/ng-recent/list.conf, ShowOpened=true)
    collector.showOpened = QSettings(QStringLiteral("ng-recent"), QStringLiteral("list")).value(QStringLiteral("ShowOpened"), false).toBool();
    if (collector.showOpened) {
        collector.opened();
    }
    collector.journal();
}

static void latestFirst(QList<Event> &list)
{
    std::sort(list.begin(), list.end(), [](const Event &a, const Event &b) {
        return a.when > b.when;
    });
}

QList<Event> events(int days, int limit)
{
    Collector collector;
    collect(collector, days);
    QList<Event> all = collector.newest.values();
    latestFirst(all);
    return all.mid(0, limit);
}

Listing listing(int days, int limit, int groupFrom)
{
    Collector collector;
    collect(collector, days);
    Listing result;
    // the operations worth one line: of several files
    QHash<quint64, Operation> grouped;
    for (auto it = collector.operations.cbegin(); it != collector.operations.cend(); ++it) {
        if (it->count >= groupFrom && QFileInfo(it->folder).isDir()) {
            grouped.insert(it.key(), *it);
        }
    }
    for (const Event &event : std::as_const(collector.newest)) {
        quint64 operation = grouped.contains(event.operation) ? event.operation : 0;
        if (operation == 0 && event.operation == 0) {
            // known from the dates only (the service keeps the first files of a long operation, or was not asked
            // about this one): part of an operation when it lies in its folder and changed while it went on
            for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
                const bool inside = event.path.startsWith(it->folder + QLatin1Char('/')) || event.path == it->folder;
                if (inside && event.when >= it->first - 2000 && event.when <= it->last + 2000 && (event.kind == Created || event.kind == Modified || event.kind == Added)) {
                    operation = it.key();
                    break;
                }
            }
        }
        if (operation != 0) {
            result.members[operation].append(event);
        } else {
            result.top.append(event);
        }
    }
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
        if (!result.members.contains(it.key())) {
            continue; // (every file of it was changed again since, or is gone: nothing to open it on)
        }
        // One line when the files belong together: all in one folder, or in a folder the operation itself made
        // (an archive unpacked, a folder copied), or so many that nothing else can be meant (a synchronisation).
        // A few files changed at the same time in different folders have only the moment in common: they stay
        // lines of their own
        const QList<Event> members = result.members.value(it.key());
        QSet<QString> folders;
        bool madeTheFolder = false;
        for (const Event &member : members) {
            if (member.folder) {
                madeTheFolder |= member.path == it->folder && member.kind == Created;
            } else {
                folders.insert(member.path.left(member.path.lastIndexOf(QLatin1Char('/'))));
            }
        }
        // (what works on its own is one line from the start: nobody did those files one by one)
        if (folders.size() > 1 && !madeTheFolder && it->count < 20 && !(it->kinds & 32)) {
            result.top += result.members.take(it.key());
            continue;
        }
        Event group;
        group.kind = ManyChanged;
        group.path = it->folder;
        group.folder = true;
        group.when = it->last;
        group.operation = it.key();
        group.count = it->count;
        group.made = (it->kinds & 1) ? 1 : (it->kinds & 4) ? 2 : 0;
        group.by = it->by;
        group.automatic = it->kinds & 32;
        group.detail = QString::number(it->count);
        result.top.append(group);
        latestFirst(result.members[it.key()]);
    }
    latestFirst(result.top);
    result.top = result.top.mid(0, limit);
    return result;
}

QString describe(const Event &event)
{
    const QString from = event.source.isEmpty() ? QString() : QFileInfo(event.source).fileName();
    // (done on its own: "by" the program, and said so; done by hand: "with" the application)
    const QString with = event.by.isEmpty() ? (event.automatic ? tr2(", automatic", ", automatico") : QString())
        : event.automatic ? tr2(", automatic (", ", automatico (") + event.by + QLatin1Char(')') : tr2(" with ", " con ") + event.by;
    switch (event.kind) {
    case Created:
        return (event.folder ? tr2("New folder", "Nuova cartella") : tr2("New", "Nuovo"))
            + (event.automatic ? with : event.by.isEmpty() ? QString() : tr2(", made with ", ", creato con ") + event.by);
    case Added:
        return (from.isEmpty() ? tr2("Added", "Aggiunto") : tr2("Added, from ", "Aggiunto, da ") + from) + with;
    case Modified:
        return tr2("Modified", "Modificato") + with;
    case Downloaded:
        return (event.detail.isEmpty() ? tr2("Downloaded", "Scaricato") : tr2("Downloaded from ", "Scaricato da ") + event.detail) + with;
    case Made:
        return (event.detail.isEmpty() ? tr2("Made", "Prodotto") : event.detail) + (from.isEmpty() ? QString() : tr2(", from ", ", da ") + from);
    case Opened:
        return tr2("Opened", "Aperto");
    case ManyChanged: {
        const QString number = QLocale().toString(event.count > 0 ? event.count : event.detail.toInt());
        return number + (event.made == 1 ? tr2(" new files", " nuovi file") : event.made == 2 ? tr2(" files added", " file aggiunti") : tr2(" files changed", " file modificati")) + with;
    }
    }
    return QString();
}
}
