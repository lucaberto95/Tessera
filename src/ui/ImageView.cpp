#include "ui/ImageView.h"
#include "ui/UiUtils.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QHelpEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPen>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double MinZoom = 0.02;
constexpr double MaxZoom = 50.0;
constexpr int ClickTolerance = 4; // pixels of the window: a smaller movement is a click, not a drag
} // namespace

ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setBackgroundBrush(QColor(0x4a4a4a));
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    viewport()->setCursor(Qt::CrossCursor);

    m_selectionItem = m_scene->addRect(QRectF(), QPen(QColor(0x1565c0), 2, Qt::DashLine), QColor(21, 101, 192, 40));
    QPen pen = m_selectionItem->pen();
    pen.setCosmetic(true);
    m_selectionItem->setPen(pen);
    m_selectionItem->setZValue(10000);
    m_selectionItem->setVisible(false);
}

void ImageView::setImage(const QImage &image)
{
    clear();
    m_image = image;
    if (m_image.isNull())
        return;
    m_pixmap = m_scene->addPixmap(QPixmap::fromImage(m_image));
    m_pixmap->setZValue(0);
    m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(m_image.size())));
    fitToWindow();
}

void ImageView::clear()
{
    clearSelection();
    for (QGraphicsRectItem *item : std::as_const(m_items))
        delete item;
    m_items.clear();
    m_areas.clear();
    m_highlighted = 0;
    delete m_pixmap;
    m_pixmap = nullptr;
    m_image = QImage();
    m_scene->setSceneRect(QRectF());
    m_pressed = m_dragging = m_panning = false;
}

void ImageView::setAreas(const QList<Area> &areas)
{
    for (QGraphicsRectItem *item : std::as_const(m_items))
        delete item;
    m_items.clear();
    m_areas = areas;
    // Smaller areas on top, so a nested area stays visible (and clickable) over the one around it.
    const double total = std::max(1.0, double(m_image.width()) * m_image.height());
    for (const Area &a : std::as_const(m_areas)) {
        auto *item = m_scene->addRect(a.rect);
        item->setZValue(1.0 + 100.0 * (1.0 - std::min(1.0, a.rect.width() * a.rect.height() / total)));
        m_items.insert(a.id, item);
    }
    if (!m_items.contains(m_highlighted))
        m_highlighted = 0;
    updateAreaStyles();
}

void ImageView::updateAreaStyles()
{
    for (const Area &a : std::as_const(m_areas)) {
        QGraphicsRectItem *item = m_items.value(a.id);
        if (!item)
            continue;
        const bool on = a.id == m_highlighted;
        QColor color = a.color.isValid() ? a.color : QColor(Qt::gray);
        QPen pen(color, on ? 3 : 2);
        pen.setCosmetic(true);
        item->setPen(pen);
        color.setAlpha(on ? 120 : 60);
        item->setBrush(color);
    }
}

void ImageView::highlightArea(qint64 id, bool scrollTo)
{
    m_highlighted = m_items.contains(id) ? id : 0;
    updateAreaStyles();
    if (m_highlighted && scrollTo)
        ensureVisible(m_items.value(m_highlighted), 40, 40);
}

QList<qint64> ImageView::areasAt(const QPointF &p) const
{
    QList<const Area *> hits;
    for (const Area &a : m_areas)
        if (a.rect.contains(p))
            hits << &a;
    std::stable_sort(hits.begin(), hits.end(), [](const Area *x, const Area *y) {
        return x->rect.width() * x->rect.height() < y->rect.width() * y->rect.height();
    });
    QList<qint64> ids;
    for (const Area *a : hits)
        ids << a->id;
    return ids;
}

void ImageView::setSelection(const QRectF &rect)
{
    QRectF r;
    if (!m_image.isNull() && !rect.isNull()) {
        r = QRectF(rect.normalized().toAlignedRect()).intersected(QRectF(QPointF(0, 0), QSizeF(m_image.size())));
        if (r.width() < 1 || r.height() < 1)
            r = QRectF();
    }
    m_selectionItem->setRect(r);
    m_selectionItem->setVisible(!r.isEmpty());
    if (r == m_selection)
        return;
    m_selection = r;
    emit selectionChanged();
}

void ImageView::applyZoom(double zoom)
{
    zoom = std::clamp(zoom, MinZoom, MaxZoom);
    resetTransform();
    scale(zoom, zoom);
    m_zoom = zoom;
    emit zoomChanged();
}

void ImageView::setZoom(double zoom)
{
    m_fit = false;
    const QPointF center = mapToScene(viewport()->rect().center());
    applyZoom(zoom);
    centerOn(center);
}

void ImageView::fitToWindow()
{
    m_fit = true;
    if (m_image.isNull())
        return;
    const QSize avail = viewport()->size() - QSize(2, 2);
    const double z = std::min({double(avail.width()) / m_image.width(), double(avail.height()) / m_image.height(), 1.0});
    applyZoom(z > 0 ? z : 1.0);
    centerOn(QPointF(m_image.width() / 2.0, m_image.height() / 2.0));
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fit)
        fitToWindow();
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull() || event->angleDelta().y() == 0) {
        event->ignore();
        return;
    }
    m_fit = false;
    const double target = std::clamp(m_zoom * std::pow(1.0015, event->angleDelta().y()), MinZoom, MaxZoom);
    const double factor = target / m_zoom;
    if (factor != 1.0) {
        scale(factor, factor); // anchored under the mouse
        m_zoom = target;
        emit zoomChanged();
    }
    event->accept();
}

QPointF ImageView::clampedScenePos(const QPoint &viewportPos) const
{
    const QPointF p = mapToScene(viewportPos);
    return {std::clamp(p.x(), 0.0, double(m_image.width())), std::clamp(p.y(), 0.0, double(m_image.height()))};
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (m_image.isNull()) {
        QGraphicsView::mousePressEvent(event);
        return;
    }
    setFocus();
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_dragging = false;
        m_pressPos = event->position().toPoint();
        m_pressScenePos = clampedScenePos(m_pressPos);
        event->accept();
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
    }
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    if (m_panning) {
        const QPoint d = pos - m_lastPanPos;
        m_lastPanPos = pos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        event->accept();
        return;
    }
    if (m_pressed) {
        if (!m_dragging && (pos - m_pressPos).manhattanLength() > ClickTolerance)
            m_dragging = true;
        if (m_dragging) {
            const QRectF r = QRectF(m_pressScenePos, clampedScenePos(pos)).normalized();
            m_selectionItem->setRect(r);
            m_selectionItem->setVisible(true);
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        viewport()->setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        if (m_dragging) {
            m_dragging = false;
            setSelection(m_selectionItem->rect()); // too small a rectangle clears the selection
        } else {
            const QList<qint64> ids = areasAt(m_pressScenePos);
            if (ids.isEmpty())
                clearSelection();
            else
                emit areaClicked(ids);
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_image.isNull())
        return;
    emit areaContextMenu(areasAt(mapToScene(event->pos())), event->globalPos());
}

bool ImageView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        QStringList tips;
        for (qint64 id : areasAt(mapToScene(help->pos())))
            for (const Area &a : m_areas)
                if (a.id == id)
                    tips << a.tip;
        if (tips.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), Ui::plainToolTip(tips.join(QLatin1Char('\n'))), viewport());
        return true;
    }
    return QGraphicsView::viewportEvent(event);
}

void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void ImageView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit codeDropped(event->mimeData()->data(QString::fromLatin1(Ui::CodeMimeType)).toLongLong(),
                     mapToScene(event->position().toPoint()));
}
