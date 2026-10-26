#pragma once

#include "overlay.h"

#include <QObject>
#include <QPointer>

class QAction;
class QSystemTrayIcon;
class SettingsDialog;

class App : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "local.shotx")

public:
    explicit App(QObject *parent = nullptr);

public Q_SLOTS:
    void Region();
    void RegionEdit();
    void Screen();
    void Window();
    void TakeShortcuts();
    void Configure();

private:
    void startRegion(Action releaseAction);
    void handle(const QImage &image, Action action);
    void save(const QImage &image);
    void notify(const QString &title, const QString &body);

    QSystemTrayIcon *m_tray;
    QList<QAction *> m_globalActions;
    QPointer<SettingsDialog> m_settings;
    bool m_selecting = false;
};
