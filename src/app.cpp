#include "app.h"
#include "capture.h"
#include "editor.h"
#include "settings.h"

#include <KGlobalAccel>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QUrl>

#include <utility>

namespace {

const QString notificationsService = QStringLiteral("org.freedesktop.Notifications");
const QString notificationsPath = QStringLiteral("/org/freedesktop/Notifications");

}

App::App(QObject *parent)
    : QObject(parent)
    , m_tray(new QSystemTrayIcon(QIcon::fromTheme(QStringLiteral("applets-screenshooter")), this))
{
    struct Entry {
        const char *id;
        QString text;
        QList<QKeySequence> keys;
        void (App::*slot)();
    };
    const Entry entries[] = {
        {"region", QStringLiteral("Capture region"), {QKeySequence(Qt::Key_Print), QKeySequence(QStringLiteral("Meta+Shift+S"))}, &App::Region},
        {"region-edit", QStringLiteral("Capture region and edit"), {QKeySequence(QStringLiteral("Ctrl+Print"))}, &App::RegionEdit},
        {"screen", QStringLiteral("Capture current screen"), {QKeySequence(QStringLiteral("Shift+Print"))}, &App::Screen},
        {"window", QStringLiteral("Capture active window"), {QKeySequence(QStringLiteral("Meta+Print"))}, &App::Window},
    };

    auto *menu = new QMenu();
    for (const Entry &entry : entries) {
        auto *action = new QAction(entry.text, this);
        action->setObjectName(QLatin1String(entry.id));
        KGlobalAccel::self()->setDefaultShortcut(action, entry.keys);
        KGlobalAccel::self()->setShortcut(action, entry.keys);
        connect(action, &QAction::triggered, this, entry.slot);
        m_globalActions.append(action);
        menu->addAction(action);
    }
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("folder-pictures")), QStringLiteral("Open screenshots folder"), this, [] {
        const QString dir = Settings::load().folder;
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), QStringLiteral("Configure ShotX…"), this, &App::Configure);
    menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit"), qApp, &QApplication::quit);

    m_tray->setToolTip(QStringLiteral("ShotX"));
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            Region();
        }
    });
    m_tray->show();

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(notificationsService, notificationsPath, notificationsService, QStringLiteral("ActivationToken"), this,
                SLOT(notificationToken(uint, QString)));
    bus.connect(notificationsService, notificationsPath, notificationsService, QStringLiteral("ActionInvoked"), this,
                SLOT(notificationAction(uint, QString)));
    bus.connect(notificationsService, notificationsPath, notificationsService, QStringLiteral("NotificationClosed"), this,
                SLOT(notificationClosed(uint, uint)));
}

void App::Region()
{
    startRegion(Action::Default);
}

void App::RegionEdit()
{
    startRegion(Action::Edit);
}

void App::Screen()
{
    QString error;
    const QImage image = Capture::activeScreen(&error);
    if (image.isNull()) {
        notify(QStringLiteral("Screenshot failed"), error);
        return;
    }
    handle(image, Action::Default);
}

void App::Window()
{
    QString error;
    const QImage image = Capture::activeWindow(&error);
    if (image.isNull()) {
        notify(QStringLiteral("Screenshot failed"), error);
        return;
    }
    handle(image, Action::Default);
}

void App::TakeShortcuts()
{
    for (QAction *action : std::as_const(m_globalActions)) {
        const QList<QKeySequence> keys = KGlobalAccel::self()->defaultShortcut(action);
        for (const QKeySequence &key : keys) {
            KGlobalAccel::stealShortcutSystemwide(key);
        }
        KGlobalAccel::self()->setShortcut(action, keys, KGlobalAccel::NoAutoloading);
    }
}

void App::Configure()
{
    if (!m_settings) {
        m_settings = new SettingsDialog(m_globalActions);
    }
    m_settings->show();
    m_settings->raise();
    m_settings->activateWindow();
}

void App::startRegion(Action releaseAction)
{
    if (m_selecting) {
        return;
    }
    QString error;
    const QList<ScreenShot> shots = Capture::allScreens(&error);
    if (shots.isEmpty()) {
        notify(QStringLiteral("Screenshot failed"), error);
        return;
    }

    m_selecting = true;
    auto *selector = new RegionSelector(shots, releaseAction, this);
    connect(selector, &RegionSelector::selected, this, &App::handle);
    connect(selector, &QObject::destroyed, this, [this] { m_selecting = false; });
    selector->show();
}

void App::handle(const QImage &image, Action action)
{
    switch (action) {
    case Action::Default: {
        const Settings settings = Settings::load();
        if (settings.copy) {
            QApplication::clipboard()->setImage(image);
        }
        if (settings.save) {
            save(image);
        }
        break;
    }
    case Action::Copy:
        QApplication::clipboard()->setImage(image);
        break;
    case Action::Save:
        save(image);
        break;
    case Action::Edit: {
        auto *editor = new Editor(image);
        connect(editor, &Editor::exportRequested, this, &App::handle);
        editor->show();
        editor->raise();
        editor->activateWindow();
        break;
    }
    }
}

void App::save(const QImage &image)
{
    const Settings settings = Settings::load();
    const QString dir = settings.folder;
    const QString path = settings.filePath(QDateTime::currentDateTime());
    const bool notifySaved = settings.notify;
    QThreadPool::globalInstance()->start([this, image, dir, path, notifySaved] {
        QDir().mkpath(dir);
        const bool ok = image.save(path);
        QMetaObject::invokeMethod(this, [this, ok, path, notifySaved] {
            if (ok) {
                if (notifySaved) {
                    this->notifySaved(path);
                }
            } else {
                notify(QStringLiteral("Could not save screenshot"), path);
            }
        });
    });
}

void App::notify(const QString &title, const QString &body)
{
    m_tray->showMessage(title, body, QIcon::fromTheme(QStringLiteral("applets-screenshooter")), 3000);
}

// Sent directly instead of through the tray icon, which cannot attach actions to a notification.
void App::notifySaved(const QString &path)
{
    QDBusMessage message = QDBusMessage::createMethodCall(notificationsService, notificationsPath, notificationsService, QStringLiteral("Notify"));
    message << QStringLiteral("ShotX") << 0u << QStringLiteral("applets-screenshooter") << QStringLiteral("Screenshot saved") << path
            << QStringList{QStringLiteral("default"), QStringLiteral("Show in folder"), QStringLiteral("show"), QStringLiteral("Show in folder")}
            << QVariantMap() << 3000;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message);
    if (reply.type() == QDBusMessage::ReplyMessage) {
        m_savedNotifications.insert(reply.arguments().value(0).toUInt(), path);
    }
}

// Emitted just before ActionInvoked; lets the file manager take focus on Wayland.
void App::notificationToken(uint id, const QString &token)
{
    if (m_savedNotifications.contains(id)) {
        m_activationToken = token;
    }
}

void App::notificationAction(uint id, const QString &action)
{
    const QString path = m_savedNotifications.value(id);
    if (path.isEmpty() || (action != QStringLiteral("default") && action != QStringLiteral("show"))) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
                                                          QStringLiteral("/org/freedesktop/FileManager1"),
                                                          QStringLiteral("org.freedesktop.FileManager1"),
                                                          QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(path).toString()} << std::exchange(m_activationToken, QString());
    QDBusConnection::sessionBus().send(message);
}

void App::notificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason)
    m_savedNotifications.remove(id);
}
