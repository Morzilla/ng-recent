// SPDX-License-Identifier: LGPL-2.1-or-later
//
// ng-recentd: the list of what happened lately to the user's files, for any program, on the session bus
// (org.ng.Recent1). Programs tell it what they did to a file (Note) and ask for the list (Events).
//
// It is started by the bus when someone calls it and leaves after a minute with nothing to do: what it is told
// is written to the journal at once (~/.local/share/ng-recent/journal.tsv), so nothing is kept in memory and
// nothing is lost while it, or the file manager, is not running. The list itself is made when asked for, from
// that journal, the ng-fsevents system service where it runs, the dates of the files and the desktop's recent
// documents: see events.h.

#include "events.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusError>
#include <QDBusMetaType>
#include <QFileInfo>
#include <QTimer>

Q_DECLARE_METATYPE(NgRecent::Event)

QDBusArgument &operator<<(QDBusArgument &argument, const NgRecent::Event &event)
{
    static const char *const kinds[] = {"created", "added", "modified", "downloaded", "made", "opened", "many-changed"};
    argument.beginStructure();
    argument << event.path << qlonglong(event.when) << QString::fromLatin1(kinds[qBound(0, int(event.kind), 6)]) << event.detail << event.source << event.by << event.folder
             << NgRecent::describe(event);
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, NgRecent::Event &event)
{
    // (never read here: the type only goes out)
    QString kind, said;
    qlonglong when = 0;
    argument.beginStructure();
    argument >> event.path >> when >> kind >> event.detail >> event.source >> event.by >> event.folder >> said;
    argument.endStructure();
    event.when = when;
    return argument;
}

class Recent : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ng.Recent1")

public:
    explicit Recent(QObject *parent = nullptr)
        : QObject(parent)
    {
        qDBusRegisterMetaType<NgRecent::Event>();
        qDBusRegisterMetaType<QList<NgRecent::Event>>();
        m_idle.setSingleShot(true);
        m_idle.setInterval(60 * 1000);
        connect(&m_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
        m_idle.start();
    }

public Q_SLOTS:
    /**
     * The calling program did something to @p path (an absolute path of a file or folder that exists).
     * @p kind: "created", "added" (copied or moved there), "modified", "downloaded" (@p detail: the site),
     * "made" (@p detail: what made it, "Convert to MP4"; @p source: the file it was made from), "opened".
     * The caller's application is found out here and kept with the note.
     */
    void Note(const QString &path, const QString &kind, const QString &detail, const QString &source)
    {
        m_idle.start();
        static const QStringList kinds{QStringLiteral("created"), QStringLiteral("added"), QStringLiteral("modified"), QStringLiteral("downloaded"),
                                       QStringLiteral("made"),    QStringLiteral("opened")};
        const int index = kinds.indexOf(kind);
        const QFileInfo info(path);
        if (index < 0 || !info.isAbsolute() || !info.exists()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("an absolute path that exists, and a kind among: ") + kinds.join(QLatin1Char(' ')));
            return;
        }
        if (path.size() > 4096 || detail.size() > 512 || source.size() > 4096) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("too long"));
            return;
        }
        const auto pid = connection().interface()->servicePid(message().service());
        NgRecent::note(info.absoluteFilePath(), NgRecent::Kind(index), detail, source, pid.isValid() ? NgRecent::applicationOf(pid.value()) : QString());
        Q_EMIT Changed();
    }

    /**
     * What happened lately to the user's files and folders, the latest first: the newest event of each, @p days
     * back (1 to 90), at most @p limit (1 to 2000). Each is (path, when in milliseconds since the epoch, kind,
     * detail, source, application, is a folder, the event in words in the user's language).
     */
    QList<NgRecent::Event> Events(int days, uint limit)
    {
        m_idle.start();
        return NgRecent::events(qBound(1, days, 90), int(qBound(1u, limit, 2000u)));
    }

Q_SIGNALS:
    /** A program told what it did: the list has something new. (Only while the service runs: it is not a way
     *  to be told of every change, the list is.) */
    void Changed();

private:
    QTimer m_idle;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ng-recentd"));
    Recent recent;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/ng/Recent1"), &recent, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)
        || !bus.registerService(QStringLiteral("org.ng.Recent1"))) {
        qWarning("ng-recentd: cannot register org.ng.Recent1 on the session bus: %s", qPrintable(bus.lastError().message()));
        return 1;
    }
    return app.exec();
}

#include "daemon.moc"
