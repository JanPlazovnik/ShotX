#pragma once

#include "capture.h"

#include <QObject>

class QKeyEvent;
class RegionOverlay;

enum class Action { Default, Copy, Save, Edit };

// Shows one frozen overlay per screen and lets the user pick a region on any of them.
class RegionSelector : public QObject
{
    Q_OBJECT

public:
    RegionSelector(const QList<ScreenShot> &shots, Action releaseAction, QObject *parent = nullptr);
    ~RegionSelector() override;

    void show();

Q_SIGNALS:
    void selected(const QImage &image, Action action);
    void cancelled();

private:
    friend class RegionOverlay;

    void setActive(RegionOverlay *overlay);
    void claim(RegionOverlay *overlay);
    void key(QKeyEvent *event);
    void finish(const QImage &image, Action action);
    void cancel();

    QList<RegionOverlay *> m_overlays;
    RegionOverlay *m_active = nullptr;
    Action m_releaseAction;
    bool m_magnifier = true;
    bool m_finished = false;
};
