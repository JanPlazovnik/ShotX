#include "editor.h"

#include <QActionGroup>
#include <QColorDialog>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QToolBar>
#include <QToolButton>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <optional>

namespace {

enum class Tool { Arrow, Rect, Ellipse, Line, Pen, Highlight, Text, Blur, Step, Crop };

struct Item {
    Tool tool;
    QColor color;
    int width;
    QPointF a;
    QPointF b;
    QPolygonF path;
    QString text;
    int number = 0;
};

QColor contrastingText(const QColor &color)
{
    return qGray(color.rgb()) > 160 ? Qt::black : Qt::white;
}

void drawItem(QPainter &p, const Item &item, const QImage &base)
{
    const QRectF rect = QRectF(item.a, item.b).normalized();
    QPen pen(item.color, item.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.save();
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (item.tool) {
    case Tool::Rect:
        pen.setJoinStyle(Qt::MiterJoin);
        p.setPen(pen);
        p.drawRect(rect);
        break;
    case Tool::Ellipse:
        p.drawEllipse(rect);
        break;
    case Tool::Line:
        p.drawLine(item.a, item.b);
        break;
    case Tool::Arrow: {
        const QLineF line(item.a, item.b);
        if (line.length() < 1) {
            break;
        }
        const qreal head = 10 + item.width * 3;
        const QPointF dir = (item.b - item.a) / line.length();
        const QPointF normal(-dir.y(), dir.x());
        const QPointF base = item.b - dir * head;
        p.drawLine(item.a, item.b - dir * head * 0.6);
        p.setPen(Qt::NoPen);
        p.setBrush(item.color);
        p.drawPolygon(QPolygonF{item.b, base + normal * head * 0.55, base - normal * head * 0.55});
        break;
    }
    case Tool::Pen:
        p.drawPolyline(item.path);
        break;
    case Tool::Highlight:
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
        p.fillRect(rect, item.color);
        break;
    case Tool::Text: {
        QFont font = p.font();
        font.setPixelSize(10 + item.width * 4);
        font.setBold(true);
        QPainterPath path;
        path.addText(item.a, font, item.text);
        p.setPen(QPen(contrastingText(item.color), qMax(2, item.width / 2 + 2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.fillPath(path, item.color);
        break;
    }
    case Tool::Blur: {
        const QRect area = rect.toAlignedRect().intersected(base.rect());
        if (area.isEmpty()) {
            break;
        }
        const int block = qMax(4, item.width * 2);
        const QImage small = base.copy(area).scaled(qMax(1, area.width() / block), qMax(1, area.height() / block),
                                                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(area, small);
        break;
    }
    case Tool::Step: {
        const qreal radius = 12 + item.width * 2;
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(item.color);
        p.drawEllipse(item.a, radius, radius);
        QFont font = p.font();
        font.setPixelSize(qRound(radius * 1.1));
        font.setBold(true);
        p.setFont(font);
        p.setPen(contrastingText(item.color));
        p.drawText(QRectF(item.a - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)), Qt::AlignCenter,
                   QString::number(item.number));
        break;
    }
    case Tool::Crop:
        break;
    }
    p.restore();
}

}

class Canvas : public QWidget
{
public:
    explicit Canvas(const QImage &image)
        : m_image(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
    {
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setMinimumSize(200, 150);
    }

    Tool tool = Tool::Arrow;
    QColor color = QColor(237, 21, 21);
    int penWidth = 4;
    std::function<void(int)> onWheelWidth;

    void undo()
    {
        if (!m_items.isEmpty()) {
            m_redo.append(m_items.takeLast());
            update();
        }
    }

    void redo()
    {
        if (!m_redo.isEmpty()) {
            m_items.append(m_redo.takeLast());
            update();
        }
    }

    QImage render() const
    {
        QImage out = m_image;
        {
            QPainter p(&out);
            p.setRenderHint(QPainter::Antialiasing);
            for (const Item &item : m_items) {
                drawItem(p, item, m_image);
            }
        }
        const QRectF crop = cropRect();
        return crop.isEmpty() ? out : out.copy(crop.toAlignedRect().intersected(out.rect()));
    }

    QSize preferredSize() const { return (QSizeF(m_image.size()) / devicePixelRatioF()).toSize(); }

protected:
    void resizeEvent(QResizeEvent *) override { m_scaled = QPixmap(); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(38, 39, 43));

        const qreal s = scale();
        const QPointF o = offset();
        if (m_scaled.isNull()) {
            const qreal dpr = devicePixelRatioF();
            m_scaled = QPixmap::fromImage(m_image.scaled((QSizeF(m_image.size()) * s * dpr).toSize(),
                                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_scaled.setDevicePixelRatio(dpr);
        }
        p.drawPixmap(o, m_scaled);

        p.translate(o);
        p.scale(s, s);
        p.setClipRect(m_image.rect());
        p.setRenderHint(QPainter::Antialiasing);
        for (const Item &item : m_items) {
            drawItem(p, item, m_image);
        }
        if (m_current) {
            drawItem(p, *m_current, m_image);
        }

        const QRectF crop = cropRect();
        if (!crop.isEmpty()) {
            QPainterPath outside;
            outside.addRect(m_image.rect());
            outside.addRect(crop);
            p.fillPath(outside, QColor(0, 0, 0, 150));
            p.setPen(QPen(Qt::white, 1 / s, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(crop);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        const QPointF pos = toImage(event->position());

        if (tool == Tool::Text) {
            bool ok = false;
            const QString text = QInputDialog::getText(this, QStringLiteral("Text"), QStringLiteral("Text:"),
                                                       QLineEdit::Normal, QString(), &ok);
            if (ok && !text.isEmpty()) {
                push(Item{tool, color, penWidth, pos, pos, {}, text});
            }
            return;
        }
        if (tool == Tool::Step) {
            int number = 1;
            for (const Item &item : std::as_const(m_items)) {
                if (item.tool == Tool::Step) {
                    number = item.number + 1;
                }
            }
            push(Item{tool, color, penWidth, pos, pos, {}, {}, number});
            return;
        }

        m_current = Item{tool, color, penWidth, pos, pos, QPolygonF{pos}};
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_current) {
            return;
        }
        QPointF pos = toImage(event->position());
        if (event->modifiers() & Qt::ShiftModifier) {
            pos = constrained(m_current->a, pos, m_current->tool);
        }
        m_current->b = pos;
        if (m_current->tool == Tool::Pen) {
            m_current->path << pos;
        }
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !m_current) {
            return;
        }
        Item item = *m_current;
        m_current.reset();
        const bool meaningful = item.tool == Tool::Pen ? item.path.size() > 1 : QLineF(item.a, item.b).length() > 2;
        if (meaningful) {
            push(item);
        } else {
            update();
        }
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (onWheelWidth && event->angleDelta().y() != 0) {
            onWheelWidth(event->angleDelta().y() > 0 ? 1 : -1);
        }
    }

private:
    void push(const Item &item)
    {
        m_items.append(item);
        m_redo.clear();
        update();
    }

    QRectF cropRect() const
    {
        if (m_current && m_current->tool == Tool::Crop) {
            return QRectF(m_current->a, m_current->b).normalized();
        }
        for (auto it = m_items.crbegin(); it != m_items.crend(); ++it) {
            if (it->tool == Tool::Crop) {
                return QRectF(it->a, it->b).normalized();
            }
        }
        return {};
    }

    static QPointF constrained(const QPointF &a, const QPointF &b, Tool tool)
    {
        const QPointF d = b - a;
        if (tool == Tool::Line || tool == Tool::Arrow) {
            const qreal angle = qRound(qAtan2(d.y(), d.x()) / (M_PI / 4)) * (M_PI / 4);
            const qreal length = QLineF(a, b).length();
            return a + QPointF(qCos(angle), qSin(angle)) * length;
        }
        const qreal side = qMax(qAbs(d.x()), qAbs(d.y()));
        return a + QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
    }

    qreal scale() const
    {
        return std::min({qreal(width()) / m_image.width(), qreal(height()) / m_image.height(), 1 / devicePixelRatioF()});
    }

    QPointF offset() const
    {
        const qreal s = scale();
        return QPointF((width() - m_image.width() * s) / 2, (height() - m_image.height() * s) / 2);
    }

    QPointF toImage(const QPointF &pos) const { return (pos - offset()) / scale(); }

    QImage m_image;
    mutable QPixmap m_scaled;
    QList<Item> m_items;
    QList<Item> m_redo;
    std::optional<Item> m_current;
};

Editor::Editor(const QImage &image, QWidget *parent)
    : QMainWindow(parent)
    , m_canvas(new Canvas(image))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("ShotX Editor"));
    setCentralWidget(m_canvas);

    auto *tools = addToolBar(QStringLiteral("Tools"));
    tools->setMovable(false);
    tools->setIconSize(QSize(22, 22));
    auto *toolGroup = new QActionGroup(this);

    const auto addTool = [&](Tool tool, const QString &icon, const QString &name, const QString &key) {
        QAction *action = tools->addAction(QIcon::fromTheme(icon), name);
        action->setCheckable(true);
        action->setShortcut(QKeySequence(key));
        action->setToolTip(QStringLiteral("%1 (%2)").arg(name, key));
        action->setActionGroup(toolGroup);
        action->setChecked(tool == m_canvas->tool);
        connect(action, &QAction::triggered, this, [this, tool] {
            m_canvas->tool = tool;
        });
    };
    addTool(Tool::Arrow, QStringLiteral("draw-arrow"), QStringLiteral("Arrow"), QStringLiteral("A"));
    addTool(Tool::Rect, QStringLiteral("draw-rectangle"), QStringLiteral("Rectangle"), QStringLiteral("R"));
    addTool(Tool::Ellipse, QStringLiteral("draw-ellipse"), QStringLiteral("Ellipse"), QStringLiteral("O"));
    addTool(Tool::Line, QStringLiteral("draw-line"), QStringLiteral("Line"), QStringLiteral("L"));
    addTool(Tool::Pen, QStringLiteral("draw-freehand"), QStringLiteral("Pen"), QStringLiteral("P"));
    addTool(Tool::Highlight, QStringLiteral("draw-highlight"), QStringLiteral("Highlight"), QStringLiteral("H"));
    addTool(Tool::Text, QStringLiteral("draw-text"), QStringLiteral("Text"), QStringLiteral("T"));
    addTool(Tool::Blur, QStringLiteral("blurfx"), QStringLiteral("Pixelate"), QStringLiteral("B"));
    addTool(Tool::Step, QStringLiteral("draw-number"), QStringLiteral("Step number"), QStringLiteral("N"));
    addTool(Tool::Crop, QStringLiteral("transform-crop"), QStringLiteral("Crop"), QStringLiteral("C"));
    tools->addSeparator();

    auto *colorButton = new QToolButton(this);
    colorButton->setToolTip(QStringLiteral("Custom color"));
    const auto setColor = [this, colorButton](const QColor &color) {
        m_canvas->color = color;
        QPixmap swatch(22, 22);
        swatch.fill(color);
        colorButton->setIcon(swatch);
    };
    setColor(m_canvas->color);
    connect(colorButton, &QToolButton::clicked, this, [this, setColor] {
        const QColor color = QColorDialog::getColor(m_canvas->color, this, QStringLiteral("Annotation color"));
        if (color.isValid()) {
            setColor(color);
        }
    });
    tools->addWidget(colorButton);

    const QList<QColor> presets{QColor(237, 21, 21), QColor(255, 140, 0), QColor(255, 235, 59), QColor(46, 204, 113),
                                QColor(61, 174, 233), QColor(155, 89, 182), Qt::white, Qt::black};
    for (const QColor &preset : presets) {
        QPixmap swatch(14, 14);
        swatch.fill(preset);
        QAction *action = tools->addAction(QIcon(swatch), preset.name());
        connect(action, &QAction::triggered, this, [setColor, preset] { setColor(preset); });
    }
    tools->addSeparator();

    auto *widthBox = new QSpinBox(this);
    widthBox->setRange(1, 30);
    widthBox->setValue(m_canvas->penWidth);
    widthBox->setPrefix(QStringLiteral("Size "));
    widthBox->setToolTip(QStringLiteral("Stroke and text size (mouse wheel over the image)"));
    connect(widthBox, &QSpinBox::valueChanged, this, [this](int value) { m_canvas->penWidth = value; });
    m_canvas->onWheelWidth = [widthBox](int step) { widthBox->setValue(widthBox->value() + step); };
    tools->addWidget(widthBox);

    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tools->addWidget(spacer);

    const auto addCommand = [&](const QString &icon, const QString &name, const QList<QKeySequence> &keys, auto slot) {
        QAction *action = tools->addAction(QIcon::fromTheme(icon), name);
        action->setShortcuts(keys);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(name, keys.first().toString(QKeySequence::NativeText)));
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    addCommand(QStringLiteral("edit-undo"), QStringLiteral("Undo"), {QKeySequence::Undo}, [this] { m_canvas->undo(); });
    addCommand(QStringLiteral("edit-redo"), QStringLiteral("Redo"),
               {QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))},
               [this] { m_canvas->redo(); });
    tools->addSeparator();
    addCommand(QStringLiteral("edit-copy"), QStringLiteral("Copy"), {QKeySequence::Copy},
               [this] { Q_EMIT exportRequested(m_canvas->render(), Action::Copy); });
    addCommand(QStringLiteral("document-save"), QStringLiteral("Save"), {QKeySequence::Save},
               [this] { Q_EMIT exportRequested(m_canvas->render(), Action::Save); });
    addCommand(QStringLiteral("document-save-as"), QStringLiteral("Save As…"), {QKeySequence(QStringLiteral("Ctrl+Shift+S"))}, [this] {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save screenshot"),
                                                          QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                          QStringLiteral("Images (*.png *.jpg *.webp)"));
        if (!path.isEmpty()) {
            m_canvas->render().save(path);
        }
    });
    QAction *done = addCommand(QStringLiteral("dialog-ok-apply"), QStringLiteral("Done"),
                               {QKeySequence(QStringLiteral("Ctrl+Return"))}, [this] {
                                   Q_EMIT exportRequested(m_canvas->render(), Action::Default);
                                   close();
                               });
    if (auto *button = qobject_cast<QToolButton *>(tools->widgetForAction(done))) {
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    auto *closeAction = new QAction(this);
    closeAction->setShortcut(Qt::Key_Escape);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);
    addAction(closeAction);

    const QSize available = QGuiApplication::primaryScreen()->availableSize() * 0.85;
    resize((m_canvas->preferredSize() + QSize(24, 70)).boundedTo(available).expandedTo(QSize(900, 500)));
}
