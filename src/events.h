// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QString>

// What happened lately to the files and folders of the user: the list behind "Recent" in Dolphin NG and Dialog NG.
// It needs nothing running: the user's folders are read when the list is asked for, and what Dolphin NG, the
// desktop's recent documents and this component's journal know is added to it. Where the ng-fsevents service
// runs, it is asked too: it knows what programs did to files anywhere in the home, and which program.
namespace NgRecent
{
enum Kind {
    Created, // a new file or folder
    Added, // copied or moved here (it is older than its arrival)
    Modified, // saved again
    Downloaded, // detail: the site it came from
    Made, // the result of a quick action; detail: the action, source: the file it was made from
    Opened, // by an application (the desktop's recent documents)
    ManyChanged, // things done in one go (an archive unpacked, a synchronisation): one line that opens on its files
};

struct Event {
    QString path;
    Kind kind = Created;
    qint64 when = 0; // milliseconds since the epoch
    QString detail;
    QString source;
    QString by; // the application that did it ("GIMP"), when the ng-fsevents service says
    bool folder = false;
    quint64 operation = 0; // what it was part of (ng-fsevents); for ManyChanged: the operation itself
    int count = 0; // ManyChanged: how many files
    int made = 0; // ManyChanged: 1 new files, 2 files moved or copied there, 0 files modified
};

/** the newest event of each file and folder, the latest first; \a days back, at most \a limit */
QList<Event> events(int days = 14, int limit = 300);

/**
 * The list as a view shows it: files changed a few at a time are lines of their own; the files of one operation
 * of \a groupFrom files or more (ng-fsevents tells which were changed in one go) are behind one line, a
 * ManyChanged event whose path is the folder they have in common.
 */
struct Listing {
    QList<Event> top; // the latest first
    QHash<quint64, QList<Event>> members; // operation -> its files, the latest first
};
Listing listing(int days = 14, int limit = 300, int groupFrom = 4);

/** what the list says under a name: "Downloaded from kde.org", "Convert to MP4, from talk.mkv" */
QString describe(const Event &event);

/** one line more in the journal (for programs that know what they did: Dolphin NG, or any program through
 *  ng-recentd's Note); \a by: the application that did it, as people call it */
void note(const QString &path, Kind kind, const QString &detail = QString(), const QString &source = QString(), const QString &by = QString());

/** the name of the application process \a pid belongs to ("GIMP"), or of its program; empty for shells and the
 *  like, which act for someone else */
QString applicationOf(qint64 pid);
QString journalPath();
}
