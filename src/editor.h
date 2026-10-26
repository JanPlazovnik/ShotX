#pragma once

#include "overlay.h"

#include <QMainWindow>

class Canvas;

class Editor : public QMainWindow
{
    Q_OBJECT

public:
    explicit Editor(const QImage &image, QWidget *parent = nullptr);

Q_SIGNALS:
    void exportRequested(const QImage &image, Action action);

private:
    Canvas *m_canvas;
};
