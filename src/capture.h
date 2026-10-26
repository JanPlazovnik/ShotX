#pragma once

#include <QImage>
#include <QList>

class QScreen;

struct ScreenShot {
    QScreen *screen;
    QImage image;
};

namespace Capture {
// All captures go through KWin's ScreenShot2 interface and return native-resolution pixels.
QList<ScreenShot> allScreens(QString *error);
QImage activeScreen(QString *error);
QImage activeWindow(QString *error);
}
