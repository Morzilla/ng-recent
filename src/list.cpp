// SPDX-License-Identifier: LGPL-2.1-or-later
#include "events.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const auto all = NgRecent::events(app.arguments().value(1, QStringLiteral("14")).toInt());
    for (const NgRecent::Event &event : all) {
        out << QDateTime::fromMSecsSinceEpoch(event.when).toString(QStringLiteral("MM-dd HH:mm")) << "  " << NgRecent::describe(event).leftJustified(34) << "  " << event.path
            << (event.folder ? "/" : "") << '\n';
    }
    return 0;
}
