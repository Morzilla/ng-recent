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
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
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

class Remote : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.ng.RecentStatus")
public Q_SLOTS:
    void ShowOptions()
    {
        Q_EMIT optionsWanted();
    }
Q_SIGNALS:
    void optionsWanted();
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ng-recent-status"));
    QApplication::setQuitOnLastWindowClosed(false);
    // one per session
    if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.ng.RecentStatus"))) {
        // (the icon is there already: asked to show its options, it is told so)
        if (QCoreApplication::arguments().contains(QStringLiteral("--options"))) {
            QDBusConnection::sessionBus().call(QDBusMessage::createMethodCall(QStringLiteral("org.ng.RecentStatus"), QStringLiteral("/Status"),
                                                                              QStringLiteral("org.ng.RecentStatus"), QStringLiteral("ShowOptions")),
                                               QDBus::Block, 2000);
        }
        return 0;
    }

    KStatusNotifierItem item(QStringLiteral("ng-recent-status"));
    item.setCategory(KStatusNotifierItem::SystemServices);
    item.setTitle(tr2("Recent files", "File recenti"));
    item.setIconByName(QStringLiteral("document-open-recent"));
    item.setStandardActionsEnabled(true);

    QSettings settings(QStringLiteral("ng-recent"), QStringLiteral("status"));
    QAction *refreshAction = item.contextMenu()->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr2("Check again", "Controlla di nuovo"));
    QAction *optionsAction = item.contextMenu()->addAction(QIcon::fromTheme(QStringLiteral("configure")), tr2("Options…", "Opzioni…"));
    bool quiet = settings.value(QStringLiteral("OnlyProblems"), false).toBool();

    State::Level shown = State::Fine;
    bool first = true;
    const auto refresh = [&] {
        const State state = look();
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
    // Options: a small window (one at a time), not entries of the menu, where a slip of the pointer hides the icon
    QPointer<QDialog> options;
    QObject::connect(optionsAction, &QAction::triggered, &app, [&] {
        if (options) {
            options->raise();
            options->activateWindow();
            return;
        }
        options = new QDialog;
        options->setAttribute(Qt::WA_DeleteOnClose);
        options->setWindowTitle(tr2("Recent files: options", "File recenti: opzioni"));
        options->setWindowIcon(QIcon::fromTheme(QStringLiteral("document-open-recent")));
        auto *layout = new QVBoxLayout(options);
        const State state = look();
        auto *headline = new QLabel(QStringLiteral("<b>") + state.headline.toHtmlEscaped() + QStringLiteral("</b>"));
        auto *details = new QLabel(state.lines.join(QLatin1Char('\n')));
        details->setWordWrap(true);
        details->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(headline);
        layout->addWidget(details);
        layout->addSpacing(8);
        auto *onlyProblems = new QCheckBox(tr2("Show the icon only when something is wrong", "Mostra l'icona solo se qualcosa non va"));
        onlyProblems->setChecked(quiet);
        layout->addWidget(onlyProblems);
        QSettings listSettings(QStringLiteral("ng-recent"), QStringLiteral("list"));
        auto *showOpened = new QCheckBox(tr2("List the files that were only opened too", "Elenca anche i file soltanto aperti"));
        showOpened->setChecked(listSettings.value(QStringLiteral("ShowOpened"), false).toBool());
        layout->addWidget(showOpened);
        // what the file change service leaves out for this user: a text file, opened in the editor
        auto *exclusions = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), tr2("Files and folders to leave out…", "File e cartelle da escludere…"));
        layout->addWidget(exclusions, 0, Qt::AlignLeft);
        auto *hint = new QLabel(tr2("One per line: a name with * (\"*.iso\") or a folder of the home (\"KVM/\"). Read again as soon as it is saved.",
                                    "Uno per riga: un nome con * (\"*.iso\") o una cartella della home (\"KVM/\"). Riletto appena viene salvato."));
        hint->setWordWrap(true);
        hint->setEnabled(false);
        layout->addWidget(hint);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        QObject::connect(exclusions, &QPushButton::clicked, options, [] {
            const QString folder = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/ng-fsevents");
            QFile file(folder + QStringLiteral("/exclude"));
            if (!file.exists() && QDir().mkpath(folder) && file.open(QIODevice::WriteOnly)) {
                file.write(tr2("# What the file change service (ng-fsevents) leaves out, one entry per line.\n"
                               "# Hidden folders and the disks of virtual machines are left out already.\n#\n"
                               "# A file name, with * and ?:        *.iso\n"
                               "# A folder of the home:             KVM/\n",
                               "# Cosa il servizio delle modifiche ai file (ng-fsevents) esclude, una voce per riga.\n"
                               "# Le cartelle nascoste e i dischi delle macchine virtuali sono già esclusi.\n#\n"
                               "# Un nome di file, con * e ?:       *.iso\n"
                               "# Una cartella della home:          KVM/\n")
                               .toUtf8());
                file.close();
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(file.fileName()));
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, options, &QDialog::reject);
        QObject::connect(buttons, &QDialogButtonBox::accepted, options, [&, onlyProblems, showOpened] {
            QSettings(QStringLiteral("ng-recent"), QStringLiteral("list")).setValue(QStringLiteral("ShowOpened"), showOpened->isChecked());
            quiet = onlyProblems->isChecked();
            settings.setValue(QStringLiteral("OnlyProblems"), quiet);
            options->accept();
            refresh();
        });
        options->show();
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
    // "ng-recent-status --options" started again reaches the icon that is there through this
    Remote remote;
    QObject::connect(&remote, &Remote::optionsWanted, optionsAction, &QAction::trigger);
    QDBusConnection::sessionBus().registerObject(QStringLiteral("/Status"), &remote, QDBusConnection::ExportAllSlots);
    if (app.arguments().contains(QStringLiteral("--options"))) {
        QTimer::singleShot(0, optionsAction, &QAction::trigger); // (the options at once: for a settings page to call)
    }
    return app.exec();
}

#include "status.moc"
