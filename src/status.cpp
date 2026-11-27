// SPDX-License-Identifier: LGPL-2.1-or-later
//
// ng-recent-status: an icon in the system tray that says whether everything behind the list of recent files
// works: the ng-fsevents system service (running? listening to this home? lost events lately?) and this
// component's own journal. The list works without the service too, from the dates of the files: that is said
// as a limitation, not as an error.

#include "events.h"

#include <KStatusNotifierItem>

#include <QAction>
#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QMenu>
#include <QSettings>
#include <QTimer>

namespace
{
QString tr2(const char *english, const char *italian)
{
    return QLocale().language() == QLocale::Italian ? QString::fromUtf8(italian) : QString::fromUtf8(english);
}

struct State {
    enum Level { Fine, Limited, Problem } level = Fine;
    QString headline;
    QStringList lines;
};

State look()
{
    State state;
    const QLocale locale;
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.ng.FsEvents1"), QStringLiteral("/org/ng/FsEvents1"), QStringLiteral("org.ng.FsEvents1"),
                                                       QStringLiteral("Status"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 1500);
    const bool running = QDBusConnection::systemBus().interface()->isServiceRegistered(QStringLiteral("org.ng.FsEvents1"));
    if (!running) {
        state.level = State::Limited;
        state.headline = tr2("The file change service is not running", "Il servizio delle modifiche ai file non è in esecuzione");
        state.lines << tr2("The list of recent files still works, from the dates of the files in your folders. It does not see "
                           "what programs change deeper in the home, nor which program did it.",
                           "La lista dei file recenti funziona lo stesso, dalle date dei file nelle tue cartelle. Non vede "
                           "ciò che i programmi modificano più in profondità nella home, né quale programma l'ha fatto.")
                    << tr2("Service: ng-fsevents (system). Start it with: systemctl enable --now ng-fsevents",
                           "Servizio: ng-fsevents (di sistema). Si avvia con: systemctl enable --now ng-fsevents");
    } else if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        state.level = State::Problem;
        state.headline = tr2("The file change service does not answer", "Il servizio delle modifiche ai file non risponde");
        state.lines << (reply.errorMessage().isEmpty() ? tr2("No answer in time.", "Nessuna risposta in tempo.") : reply.errorMessage());
    } else {
        const QVariantMap status = qdbus_cast<QVariantMap>(reply.arguments().constFirst());
        const qint64 started = status.value(QStringLiteral("started")).toLongLong();
        const qint64 overflowed = status.value(QStringLiteral("overflowed")).toLongLong();
        if (!status.value(QStringLiteral("watched")).toBool()) {
            state.level = State::Problem;
            state.headline = tr2("The service cannot listen to your home", "Il servizio non riesce ad ascoltare la tua home");
            state.lines << tr2("The system says: ", "Il sistema dice: ") + status.value(QStringLiteral("reason")).toString()
                        << tr2("(Some file systems cannot be listened to: a live system, a network folder.) The list still works from the dates of the files.",
                               "(Alcuni file system non si possono ascoltare: un sistema live, una cartella di rete.) La lista funziona lo stesso dalle date dei file.");
        } else {
            state.headline = tr2("Recent files: everything works", "File recenti: tutto funziona");
            state.lines << tr2("File change service: running since ", "Servizio delle modifiche ai file: attivo dal ")
                    + locale.toString(QDateTime::fromSecsSinceEpoch(started), QLocale::ShortFormat);
            if (overflowed > 0 && QDateTime::currentSecsSinceEpoch() - overflowed < 24 * 3600) {
                state.level = State::Limited;
                state.headline = tr2("Recent files: some changes were lost", "File recenti: alcune modifiche sono andate perse");
                state.lines << tr2("Too many changes at once on ", "Troppe modifiche insieme il ") + locale.toString(QDateTime::fromSecsSinceEpoch(overflowed), QLocale::ShortFormat)
                        + tr2(": those of that moment are not in the list.", ": quelle di quel momento non sono nella lista.");
            }
        }
        state.lines << tr2("Files in the service's journal: ", "File nel diario del servizio: ") + locale.toString(status.value(QStringLiteral("files")).toUInt())
                + tr2(" (kept ", " (tenuti ") + QString::number(status.value(QStringLiteral("days")).toUInt()) + tr2(" days)", " giorni)");
    }
    const QFileInfo journal(NgRecent::journalPath());
    state.lines << tr2("What programs told they did: ", "Dichiarazioni dei programmi: ")
            + (journal.exists() ? locale.formattedDataSize(journal.size()) + tr2(" (at most 400 KiB)", " (al massimo 400 KiB)") : tr2("none yet", "ancora nessuna"));
    return state;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ng-recent-status"));
    QApplication::setQuitOnLastWindowClosed(false);
    // one per session
    if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.ng.RecentStatus"))) {
        return 0;
    }

    KStatusNotifierItem item(QStringLiteral("ng-recent-status"));
    item.setCategory(KStatusNotifierItem::SystemServices);
    item.setTitle(tr2("Recent files", "File recenti"));
    item.setIconByName(QStringLiteral("document-open-recent"));
    item.setStandardActionsEnabled(true);

    QSettings settings(QStringLiteral("ng-recent"), QStringLiteral("status"));
    QAction *refreshAction = item.contextMenu()->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr2("Check again", "Controlla di nuovo"));
    QAction *quietAction = item.contextMenu()->addAction(tr2("Show only when something is wrong", "Mostra solo se qualcosa non va"));
    quietAction->setCheckable(true);
    quietAction->setChecked(settings.value(QStringLiteral("OnlyProblems"), false).toBool());

    State::Level shown = State::Fine;
    bool first = true;
    const auto refresh = [&] {
        const State state = look();
        const bool quiet = quietAction->isChecked();
        // (the icon itself changes: the tray draws no overlay on small icons)
        item.setIconByName(state.level == State::Problem ? QStringLiteral("data-error") : state.level == State::Limited ? QStringLiteral("data-warning") : QStringLiteral("document-open-recent"));
        item.setToolTip(QStringLiteral("document-open-recent"), state.headline, state.lines.join(QStringLiteral("<br/>")));
        // (passive: the tray keeps it among the hidden icons)
        item.setStatus(state.level == State::Problem ? KStatusNotifierItem::NeedsAttention
                           : (state.level == State::Limited || !quiet) ? KStatusNotifierItem::Active : KStatusNotifierItem::Passive);
        if (!first && state.level != shown && state.level == State::Problem) {
            item.showMessage(state.headline, state.lines.value(0), QStringLiteral("dialog-warning"), 8000);
        }
        shown = state.level;
        first = false;
    };
    QObject::connect(refreshAction, &QAction::triggered, &app, refresh);
    QObject::connect(quietAction, &QAction::toggled, &app, [&](bool on) {
        settings.setValue(QStringLiteral("OnlyProblems"), on);
        refresh();
    });
    // a click: the state said in a notification (there is no window to show)
    QObject::connect(&item, &KStatusNotifierItem::activateRequested, &app, [&] {
        refresh();
        const State state = look();
        item.showMessage(state.headline, state.lines.join(QLatin1Char('\n')), QStringLiteral("document-open-recent"), 10000);
    });

    // at once when the service comes or goes, and every few minutes for the rest
    QDBusServiceWatcher watcher(QStringLiteral("org.ng.FsEvents1"), QDBusConnection::systemBus(), QDBusServiceWatcher::WatchForOwnerChange);
    QObject::connect(&watcher, &QDBusServiceWatcher::serviceOwnerChanged, &app, [&] {
        QTimer::singleShot(1500, &app, refresh);
    });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, refresh);
    timer.start(5 * 60 * 1000);
    refresh();
    return app.exec();
}
