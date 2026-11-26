// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QList>
#include <QString>

// What happened lately to the files and folders of the user: the list behind "Recent" in Dolphin NG and Dialog NG.
// It needs nothing running: the user's folders are read when the list is asked for, and what Dolphin NG, the
// desktop's recent documents and this component's journal know is added to it.
namespace NgRecent
{
enum Kind {
    Created, // a new file or folder
    Added, // copied or moved here (it is older than its arrival)
    Modified, // saved again
    Downloaded, // detail: the site it came from
    Made, // the result of a quick action; detail: the action, source: the file it was made from
    Opened, // by an application (the desktop's recent documents)
};

struct Event {
    QString path;
    Kind kind = Created;
    qint64 when = 0; // milliseconds since the epoch
    QString detail;
    QString source;
    bool folder = false;
};

/** the newest event of each file and folder, the latest first; \a days back, at most \a limit */
QList<Event> events(int days = 14, int limit = 300);

/** what the list says under a name: "Downloaded from kde.org", "Convert to MP4, from talk.mkv" */
QString describe(const Event &event);

/** one line more in the journal (for programs that know what they did: Dolphin NG) */
void note(const QString &path, Kind kind, const QString &detail = QString(), const QString &source = QString());
QString journalPath();
}
