#pragma once

#include <QColor>
#include <QGraphicsView>
#include <QHash>
#include <QImage>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;

// Image with coded areas (issue #27). Scene coordinates are pixels of the image. The wheel zooms,
// the middle button pans, a left drag draws the selection rectangle and a click on a coded area
// selects it. The view only reports what the user does; DocumentView turns it into codings.
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    struct Area {
        qint64 id = 0;
        QRectF rect;
        QColor color;
        QString tip; // shown as plain text
    };

    explicit ImageView(QWidget *parent = nullptr);

    void setImage(const QImage &image); // fits it to the window
    void clear();
    bool hasImage() const { return !m_image.isNull(); }
    QSize imageSize() const { return m_image.size(); }

    void setAreas(const QList<Area> &areas);
    void highlightArea(qint64 id, bool scrollTo = false);
    qint64 highlightedArea() const { return m_highlighted; }
    // Areas containing the point, smallest (topmost) first.
    QList<qint64> areasAt(const QPointF &imagePos) const;
    QPointF mapToImage(const QPoint &viewportPos) const { return mapToScene(viewportPos); }

    bool hasSelection() const { return !m_selection.isEmpty(); }
    QRectF selection() const { return m_selection; } // whole pixels, inside the image
    void setSelection(const QRectF &rect);
    void clearSelection() { setSelection({}); }

    double zoom() const { return m_zoom; }
    void setZoom(double zoom); // around the center of the window
    void fitToWindow();
    void actualSize() { setZoom(1.0); }
    bool isFitted() const { return m_fit; }

signals:
    void selectionChanged();
    void areaClicked(const QList<qint64> &ids); // topmost first
    void areaContextMenu(const QList<qint64> &ids, const QPoint &globalPos);
    void codeDropped(qint64 codeId, const QPointF &imagePos);
    void zoomChanged();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    QPointF clampedScenePos(const QPoint &viewportPos) const;
    void applyZoom(double zoom);
    void updateAreaStyles();

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_pixmap = nullptr;
    QGraphicsRectItem *m_selectionItem = nullptr;
    QHash<qint64, QGraphicsRectItem *> m_items;
    QList<Area> m_areas;
    QImage m_image;
    QRectF m_selection;
    qint64 m_highlighted = 0;
    double m_zoom = 1.0;
    bool m_fit = true;

    bool m_pressed = false;
    bool m_dragging = false;
    bool m_panning = false;
    QPoint m_pressPos;
    QPoint m_lastPanPos;
    QPointF m_pressScenePos;
};
