#include "app.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>

#include <cstdio>
#include <malloc.h>

int main(int argc, char *argv[])
{
    // A fixed threshold stops glibc from raising it after the first large free, which would otherwise
    // leave freed screenshot buffers parked in per-thread arenas instead of returning them to the system.
    mallopt(M_MMAP_THRESHOLD, 1 << 20);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("shotx"));
    QApplication::setApplicationDisplayName(QStringLiteral("ShotX"));
    QApplication::setDesktopFileName(QStringLiteral("shotx"));
    QApplication::setQuitOnLastWindowClosed(false);

    const QHash<QString, QString> commands{
        {QStringLiteral("region"), QStringLiteral("Region")},
        {QStringLiteral("region-edit"), QStringLiteral("RegionEdit")},
        {QStringLiteral("screen"), QStringLiteral("Screen")},
        {QStringLiteral("window"), QStringLiteral("Window")},
        {QStringLiteral("take-shortcuts"), QStringLiteral("TakeShortcuts")},
        {QStringLiteral("settings"), QStringLiteral("Configure")},
    };
    const QStringList args = QApplication::arguments();
    QString method;
    if (args.size() > 1) {
        method = commands.value(args.at(1));
        if (method.isEmpty()) {
            std::fprintf(stderr, "usage: shotx [region|region-edit|screen|window|take-shortcuts|settings]\n");
            return 1;
        }
    }

    const QString service = QStringLiteral("local.shotx");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(service)) {
        // Launching ShotX again from the app menu opens its settings.
        bus.call(QDBusMessage::createMethodCall(service, QStringLiteral("/"), QStringLiteral("local.shotx"),
                                                method.isEmpty() ? QStringLiteral("Configure") : method));
        return 0;
    }

    App shotx;
    bus.registerObject(QStringLiteral("/"), &shotx, QDBusConnection::ExportAllSlots);
    if (!method.isEmpty()) {
        QMetaObject::invokeMethod(&shotx, method.toLatin1().constData(), Qt::QueuedConnection);
    }
    return app.exec();
}
