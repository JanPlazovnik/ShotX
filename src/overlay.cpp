#include "overlay.h"

#include <LayerShellQt/Window>

#include <QCursor>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QToolButton>
#include <QWidget>
#include <QWindow>

namespace {

const QColor accent(61, 174, 233);
const int edgeLeft = 1, edgeRight = 2, edgeTop = 4, edgeBottom = 8;
const int magnifierCells = 15;
const int magnifierCellSize = 9;

void drawPill(QPainter &p, const QRect &rect)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 20, 24, 225));
    p.drawRoundedRect(rect, 6, 6);
}

}

class RegionOverlay : public QWidget
{
public:
    RegionOverlay(RegionSelector *selector, const ScreenShot &shot)
        : m_selector(selector)
        , m_shot(shot)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);

        const QRect geometry = shot.screen->geometry();
        m_dpr = qreal(shot.image.width()) / geometry.width();

        m_bright = QPixmap::fromImage(shot.image);
        m_bright.setDevicePixelRatio(m_dpr);
        QImage dim = shot.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&dim);
            p.fillRect(dim.rect(), QColor(0, 0, 0, 115));
        }
        m_dim = QPixmap::fromImage(dim);
        m_dim.setDevicePixelRatio(m_dpr);

        buildToolbar();

        resize(geometry.size());
        create();
        windowHandle()->setScreen(shot.screen);
        if (auto *layer = LayerShellQt::Window::get(windowHandle())) {
            layer->setLayer(LayerShellQt::Window::LayerOverlay);
            layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorBottom,
                               LayerShellQt::Window::AnchorLeft, LayerShellQt::Window::AnchorRight});
            layer->setExclusiveZone(-1);
            layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
            layer->setScope(QStringLiteral("shotx"));
            layer->setScreen(shot.screen);
        }
    }

    QScreen *screen() const { return m_shot.screen; }
    bool hasSelection() const { return m_sel.width() > 0 && m_sel.height() > 0; }

    void clearSelection()
    {
        m_sel = QRect();
        m_drag = None;
        m_toolbar->hide();
        update();
    }

    void selectAll(Action action)
    {
        m_sel = rect();
        accept(action);
    }

    void accept(Action action)
    {
        if (hasSelection()) {
            m_selector->finish(cropped(), action);
        }
    }

    void handleKey(QKeyEvent *event)
    {
        const bool ctrl = event->modifiers() & Qt::ControlModifier;
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept(event->modifiers() & Qt::ShiftModifier ? Action::Edit : m_selector->m_releaseAction);
            return;
        case Qt::Key_Space:
        case Qt::Key_F:
            selectAll(m_selector->m_releaseAction);
            return;
        case Qt::Key_C:
            if (ctrl) {
                accept(Action::Copy);
            }
            return;
        case Qt::Key_S:
            if (ctrl) {
                accept(Action::Save);
            }
            return;
        case Qt::Key_E:
            accept(Action::Edit);
            return;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
            nudge(event);
            return;
        }
    }

    void refresh() { update(); }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        m_cursor = event->position().toPoint();
        m_selector->setActive(this);
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_cursor = QPoint(-1, -1);
        update();
    }

    void keyPressEvent(QKeyEvent *event) override { m_selector->key(event); }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_selector->claim(this);
        const QPoint pos = event->position().toPoint();

        if (event->button() == Qt::RightButton) {
            if (hasSelection() || m_drag != None) {
                clearSelection();
            } else {
                m_selector->cancel();
            }
            return;
        }
        if (event->button() != Qt::LeftButton) {
            return;
        }

        m_startSel = m_sel;
        m_anchor = pos;
        m_edges = m_adjusting ? edgesAt(pos) : 0;
        if (m_edges) {
            m_drag = Resize;
        } else if (m_adjusting && m_sel.contains(pos)) {
            m_drag = Move;
        } else {
            m_drag = Create;
            m_sel = QRect(pos, QSize(0, 0));
        }
        m_toolbar->hide();
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        m_cursor = event->position().toPoint();
        const QPoint pos = m_cursor;
        const QRect bounds = rect();

        switch (m_drag) {
        case Create:
            m_sel = QRect(m_anchor, pos).normalized().intersected(bounds);
            break;
        case Move: {
            QRect moved = m_startSel.translated(pos - m_anchor);
            moved.moveLeft(qBound(0, moved.left(), bounds.width() - moved.width()));
            moved.moveTop(qBound(0, moved.top(), bounds.height() - moved.height()));
            m_sel = moved;
            break;
        }
        case Resize: {
            QRect r = m_startSel;
            const QPoint delta = pos - m_anchor;
            if (m_edges & edgeLeft) r.setLeft(r.left() + delta.x());
            if (m_edges & edgeRight) r.setRight(r.right() + delta.x());
            if (m_edges & edgeTop) r.setTop(r.top() + delta.y());
            if (m_edges & edgeBottom) r.setBottom(r.bottom() + delta.y());
            m_sel = r.normalized().intersected(bounds);
            break;
        }
        case None:
            updateCursorShape(pos);
            break;
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || m_drag == None) {
            return;
        }
        const Drag drag = m_drag;
        m_drag = None;
        const bool keep = m_adjusting || (event->modifiers() & Qt::ShiftModifier);

        if (drag == Create && m_sel.width() < 4 && m_sel.height() < 4) {
            if (keep) {
                m_sel = rect();
            } else {
                selectAll(m_selector->m_releaseAction);
                return;
            }
        } else if (!keep) {
            accept(m_selector->m_releaseAction);
            return;
        }

        m_adjusting = true;
        placeToolbar();
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_adjusting && m_sel.contains(event->position().toPoint())) {
            accept(m_selector->m_releaseAction);
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.drawPixmap(QPoint(0, 0), m_dim);

        if (hasSelection()) {
            p.drawPixmap(QRectF(m_sel), m_bright, toPixels(m_sel));
            p.setPen(QPen(accent, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(m_sel).adjusted(0.5, 0.5, -0.5, -0.5));
            if (m_adjusting && m_drag == None) {
                drawHandles(p);
            }
            drawSizeLabel(p);
        } else if (rect().contains(m_cursor)) {
            p.setPen(QPen(QColor(255, 255, 255, 70), 1));
            p.drawLine(QPointF(0, m_cursor.y() + 0.5), QPointF(width(), m_cursor.y() + 0.5));
            p.drawLine(QPointF(m_cursor.x() + 0.5, 0), QPointF(m_cursor.x() + 0.5, height()));
            drawHint(p);
        }

        if (m_selector->m_magnifier && rect().contains(m_cursor) && m_drag != Move) {
            drawMagnifier(p);
        }
    }

private:
    enum Drag { None, Create, Move, Resize };

    void buildToolbar()
    {
        m_toolbar = new QWidget(this);
        m_toolbar->setAttribute(Qt::WA_StyledBackground);
        m_toolbar->setObjectName(QStringLiteral("toolbar"));
        m_toolbar->setStyleSheet(QStringLiteral(
            "#toolbar { background: rgba(20,20,24,235); border: 1px solid rgba(255,255,255,30); border-radius: 8px; }"
            "QToolButton { color: white; background: transparent; border: none; border-radius: 5px; padding: 5px 9px; }"
            "QToolButton:hover { background: rgba(61,174,233,90); }"));
        auto *layout = new QHBoxLayout(m_toolbar);
        layout->setContentsMargins(4, 4, 4, 4);
        layout->setSpacing(2);

        const auto add = [&](const QString &icon, const QString &text, const QString &shortcut, auto onClick) {
            auto *button = new QToolButton(m_toolbar);
            button->setIcon(QIcon::fromTheme(icon));
            button->setText(text);
            button->setToolTip(QStringLiteral("%1 (%2)").arg(text, shortcut));
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            button->setFocusPolicy(Qt::NoFocus);
            button->setCursor(Qt::PointingHandCursor);
            connect(button, &QToolButton::clicked, this, onClick);
            layout->addWidget(button);
        };
        add(QStringLiteral("edit-copy"), QStringLiteral("Copy"), QStringLiteral("Ctrl+C"), [this] { accept(Action::Copy); });
        add(QStringLiteral("document-save"), QStringLiteral("Save"), QStringLiteral("Ctrl+S"), [this] { accept(Action::Save); });
        add(QStringLiteral("document-edit"), QStringLiteral("Edit"), QStringLiteral("E"), [this] { accept(Action::Edit); });
        add(QStringLiteral("dialog-ok-apply"), QStringLiteral("Done"), QStringLiteral("Enter"),
            [this] { accept(m_selector->m_releaseAction); });
        add(QStringLiteral("dialog-cancel"), QString(), QStringLiteral("Esc"), [this] { m_selector->cancel(); });
        m_toolbar->adjustSize();
        m_toolbar->hide();
    }

    void placeToolbar()
    {
        const QSize size = m_toolbar->sizeHint();
        int y = m_sel.bottom() + 10;
        if (y + size.height() > height()) {
            y = m_sel.top() - size.height() - 10;
        }
        if (y < 0) {
            y = m_sel.bottom() - size.height() - 10;
        }
        const int x = qBound(4, m_sel.right() - size.width(), width() - size.width() - 4);
        m_toolbar->setGeometry(QRect(QPoint(x, y), size));
        m_toolbar->show();
        m_toolbar->raise();
    }

    QRectF toPixels(const QRect &r) const
    {
        return QRectF(r.x() * m_dpr, r.y() * m_dpr, r.width() * m_dpr, r.height() * m_dpr);
    }

    QImage cropped() const
    {
        return m_shot.image.copy(toPixels(m_sel).toAlignedRect().intersected(m_shot.image.rect()));
    }

    int edgesAt(const QPoint &pos) const
    {
        if (!hasSelection() || !m_sel.adjusted(-8, -8, 8, 8).contains(pos)) {
            return 0;
        }
        int edges = 0;
        if (qAbs(pos.x() - m_sel.left()) <= 8) edges |= edgeLeft;
        else if (qAbs(pos.x() - m_sel.right()) <= 8) edges |= edgeRight;
        if (qAbs(pos.y() - m_sel.top()) <= 8) edges |= edgeTop;
        else if (qAbs(pos.y() - m_sel.bottom()) <= 8) edges |= edgeBottom;
        return edges;
    }

    void updateCursorShape(const QPoint &pos)
    {
        if (!m_adjusting) {
            return;
        }
        const int edges = edgesAt(pos);
        Qt::CursorShape shape = Qt::CrossCursor;
        if (edges == (edgeLeft | edgeTop) || edges == (edgeRight | edgeBottom)) shape = Qt::SizeFDiagCursor;
        else if (edges == (edgeRight | edgeTop) || edges == (edgeLeft | edgeBottom)) shape = Qt::SizeBDiagCursor;
        else if (edges & (edgeLeft | edgeRight)) shape = Qt::SizeHorCursor;
        else if (edges & (edgeTop | edgeBottom)) shape = Qt::SizeVerCursor;
        else if (m_sel.contains(pos)) shape = Qt::SizeAllCursor;
        setCursor(shape);
    }

    void nudge(QKeyEvent *event)
    {
        if (!hasSelection()) {
            return;
        }
        const int step = event->modifiers() & Qt::ShiftModifier ? 10 : 1;
        const bool resize = event->modifiers() & Qt::AltModifier;
        QPoint delta;
        switch (event->key()) {
        case Qt::Key_Left: delta = {-step, 0}; break;
        case Qt::Key_Right: delta = {step, 0}; break;
        case Qt::Key_Up: delta = {0, -step}; break;
        case Qt::Key_Down: delta = {0, step}; break;
        }
        QRect r = m_sel;
        if (resize) {
            r.setSize((r.size() + QSize(delta.x(), delta.y())).expandedTo(QSize(1, 1)));
        } else {
            r.translate(delta);
            r.moveLeft(qBound(0, r.left(), width() - r.width()));
            r.moveTop(qBound(0, r.top(), height() - r.height()));
        }
        m_sel = r.intersected(rect());
        m_adjusting = true;
        placeToolbar();
        update();
    }

    void drawHandles(QPainter &p)
    {
        p.setPen(QPen(accent, 1));
        p.setBrush(Qt::white);
        const QRect r = m_sel;
        const QPoint points[] = {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight(),
                                 {r.center().x(), r.top()}, {r.center().x(), r.bottom()},
                                 {r.left(), r.center().y()}, {r.right(), r.center().y()}};
        for (const QPoint &point : points) {
            p.drawRect(QRectF(point.x() - 3.5, point.y() - 3.5, 7, 7));
        }
    }

    void drawSizeLabel(QPainter &p)
    {
        const QRect px = toPixels(m_sel).toAlignedRect();
        const QString text = QStringLiteral("%1 × %2").arg(px.width()).arg(px.height());
        QFont font = p.font();
        font.setBold(true);
        p.setFont(font);
        const QRect textRect = p.fontMetrics().boundingRect(text).adjusted(-8, -4, 8, 4);
        QRect label(QPoint(m_sel.left(), m_sel.top() - textRect.height() - 6), textRect.size());
        if (label.top() < 0) {
            label.moveTop(m_sel.top() + 6);
            label.moveLeft(m_sel.left() + 6);
        }
        drawPill(p, label);
        p.setPen(Qt::white);
        p.drawText(label, Qt::AlignCenter, text);
    }

    void drawHint(QPainter &p)
    {
        const QString text = QStringLiteral(
            "Drag: capture   ·   Shift+drag: adjust first   ·   Click / Space: whole screen   ·   M: magnifier   ·   Esc: cancel");
        const QRect textRect = p.fontMetrics().boundingRect(text).adjusted(-14, -8, 14, 8);
        QRect hint(QPoint((width() - textRect.width()) / 2, 24), textRect.size());
        if (hint.adjusted(-40, -40, 40, 40).contains(m_cursor)) {
            hint.moveTop(height() - hint.height() - 24);
        }
        drawPill(p, hint);
        p.setPen(QColor(235, 235, 235));
        p.drawText(hint, Qt::AlignCenter, text);
    }

    void drawMagnifier(QPainter &p)
    {
        const QPoint centerPx(qFloor(m_cursor.x() * m_dpr), qFloor(m_cursor.y() * m_dpr));
        const int half = magnifierCells / 2;
        QImage cells(magnifierCells, magnifierCells, QImage::Format_ARGB32_Premultiplied);
        cells.fill(Qt::black);
        {
            QPainter cp(&cells);
            cp.drawImage(QPoint(0, 0), m_shot.image,
                         QRect(centerPx - QPoint(half, half), QSize(magnifierCells, magnifierCells)));
        }

        const int size = magnifierCells * magnifierCellSize;
        const QString info = QStringLiteral("%1, %2   %3")
                                 .arg(centerPx.x())
                                 .arg(centerPx.y())
                                 .arg(m_shot.image.pixelColor(centerPx.x(), centerPx.y()).name().toUpper());
        const int infoHeight = p.fontMetrics().height() + 8;

        QPoint origin = m_cursor + QPoint(24, 24);
        if (origin.x() + size > width()) origin.setX(m_cursor.x() - 24 - size);
        if (origin.y() + size + infoHeight > height()) origin.setY(m_cursor.y() - 24 - size - infoHeight);
        const QRect box(origin, QSize(size, size));

        QPainterPath clip;
        clip.addRoundedRect(box, 8, 8);
        p.save();
        p.setClipPath(clip);
        p.drawImage(box, cells);
        p.setPen(QPen(QColor(0, 0, 0, 40), 1));
        for (int i = 1; i < magnifierCells; ++i) {
            const int offset = i * magnifierCellSize;
            p.drawLine(box.left() + offset, box.top(), box.left() + offset, box.bottom());
            p.drawLine(box.left(), box.top() + offset, box.right(), box.top() + offset);
        }
        p.restore();

        p.setPen(QPen(Qt::white, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(box.topLeft() + QPoint(half, half) * magnifierCellSize, QSize(magnifierCellSize, magnifierCellSize)));
        p.setPen(QPen(accent, 2));
        p.drawRoundedRect(box, 8, 8);

        const QRect infoRect(box.left(), box.bottom() + 4, size, infoHeight);
        drawPill(p, infoRect);
        p.setPen(Qt::white);
        p.drawText(infoRect, Qt::AlignCenter, info);
    }

    RegionSelector *m_selector;
    ScreenShot m_shot;
    QPixmap m_bright;
    QPixmap m_dim;
    qreal m_dpr = 1;
    QWidget *m_toolbar = nullptr;

    QRect m_sel;
    QRect m_startSel;
    QPoint m_anchor;
    QPoint m_cursor{-1, -1};
    Drag m_drag = None;
    int m_edges = 0;
    bool m_adjusting = false;
};

RegionSelector::RegionSelector(const QList<ScreenShot> &shots, Action releaseAction, QObject *parent)
    : QObject(parent)
    , m_releaseAction(releaseAction)
{
    for (const ScreenShot &shot : shots) {
        m_overlays.append(new RegionOverlay(this, shot));
    }
    for (RegionOverlay *overlay : std::as_const(m_overlays)) {
        if (overlay->screen()->geometry().contains(QCursor::pos())) {
            m_active = overlay;
        }
    }
    if (!m_active && !m_overlays.isEmpty()) {
        m_active = m_overlays.first();
    }
}

RegionSelector::~RegionSelector()
{
    qDeleteAll(m_overlays);
}

void RegionSelector::show()
{
    for (RegionOverlay *overlay : std::as_const(m_overlays)) {
        overlay->show();
    }
}

void RegionSelector::setActive(RegionOverlay *overlay)
{
    m_active = overlay;
}

void RegionSelector::claim(RegionOverlay *overlay)
{
    for (RegionOverlay *other : std::as_const(m_overlays)) {
        if (other != overlay && other->hasSelection()) {
            other->clearSelection();
        }
    }
    m_active = overlay;
}

void RegionSelector::key(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancel();
    } else if (event->key() == Qt::Key_M) {
        m_magnifier = !m_magnifier;
        for (RegionOverlay *overlay : std::as_const(m_overlays)) {
            overlay->refresh();
        }
    } else {
        RegionOverlay *target = m_active;
        for (RegionOverlay *overlay : std::as_const(m_overlays)) {
            if (overlay->hasSelection()) {
                target = overlay;
            }
        }
        if (target) {
            target->handleKey(event);
        }
    }
}

void RegionSelector::finish(const QImage &image, Action action)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    Q_EMIT selected(image, action);
    deleteLater();
}

void RegionSelector::cancel()
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    Q_EMIT cancelled();
    deleteLater();
}
