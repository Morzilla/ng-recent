// SPDX-License-Identifier: LGPL-2.1-or-later
#include "events.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    // the list as a view shows it: an operation of several files is one line, its files set in below it
    const NgRecent::Listing listing = NgRecent::listing(app.arguments().value(1, QStringLiteral("14")).toInt());
    const auto line = [&out](const NgRecent::Event &event, const char *indent) {
        out << indent << QDateTime::fromMSecsSinceEpoch(event.when).toString(QStringLiteral("MM-dd HH:mm")) << "  " << NgRecent::describe(event).leftJustified(34) << "  "
            << event.path << (event.folder ? "/" : "") << '\n';
    };
    for (const NgRecent::Event &event : listing.top) {
        line(event, "");
        if (event.kind == NgRecent::ManyChanged) {
            const QList<NgRecent::Event> members = listing.members.value(event.operation);
            for (int i = 0; i < members.size() && i < 3; ++i) {
                line(members.at(i), "      ");
            }
            if (members.size() > 3) {
                out << "      ... " << members.size() - 3 << " more listed\n";
            }
        }
    }
    return 0;
}
