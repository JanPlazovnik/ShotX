#include "app.h"
#include "capture.h"
#include "editor.h"

#include <KGlobalAccel>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QMenu>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QUrl>

namespace {

QString screenshotDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Screenshots");
}

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
        QDir().mkpath(screenshotDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(screenshotDir()));
    });
    menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), QStringLiteral("Quit"), qApp, &QApplication::quit);

    m_tray->setToolTip(QStringLiteral("ShotX"));
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            Region();
        }
    });
    m_tray->show();
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
    case Action::Default:
        QApplication::clipboard()->setImage(image);
        save(image);
        break;
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
    const QString dir = screenshotDir();
    const QString path = dir + QStringLiteral("/Screenshot_%1.png")
                                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz")));
    QThreadPool::globalInstance()->start([this, image, dir, path] {
        QDir().mkpath(dir);
        const bool ok = image.save(path, "PNG");
        QMetaObject::invokeMethod(this, [this, ok, path] {
            if (ok) {
                notify(QStringLiteral("Screenshot saved"), path);
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
