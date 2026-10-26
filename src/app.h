#pragma once

#include "overlay.h"

#include <QObject>

class QAction;
class QSystemTrayIcon;

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

private:
    void startRegion(Action releaseAction);
    void handle(const QImage &image, Action action);
    void save(const QImage &image);
    void notify(const QString &title, const QString &body);

    QSystemTrayIcon *m_tray;
    QList<QAction *> m_globalActions;
    bool m_selecting = false;
};
