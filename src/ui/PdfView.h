#pragma once

#include "core/Pdfs.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPolygonF>
#include <QRectF>

#include <memory>

// PDF shown as continuous pages, with coded areas (issue #26). Positions are PDF points of a page
// (origin top left), so codings do not depend on the zoom. A left drag selects text, Alt + drag
// selects a rectangle, a click on a coded area selects it; Ctrl + wheel zooms. The margin beside each
// page carries the code bars of its areas. The view only reports what the user does: DocumentView
// turns it into codings.
class PdfView : public QAbstractScrollArea {
    Q_OBJECT
public:
    struct Area {
        qint64 id = 0;
        int page = 1;
        QRectF rect; // points
        QColor color;
        QString tip; // shown as plain text
    };
    struct Selection {
        int page = 0; // 0 = nothing selected
        QRectF rect;  // points
        QString text;
        bool isText = false; // made by selecting text (the rectangle then bounds the text), not by Alt + drag
    };

    explicit PdfView(QWidget *parent = nullptr);

    bool setPdf(const QByteArray &data); // shows it fitted to the width
    void clear();
    bool hasPdf() const { return m_doc && m_doc->isValid(); }
    int pageCount() const { return m_doc ? m_doc->pageCount() : 0; }
    const Pdfs::Doc *document() const { return m_doc.get(); }

    void setAreas(const QList<Area> &areas);
    void highlightArea(qint64 id, bool scrollTo = false);
    qint64 highlightedArea() const { return m_highlighted; }
    // Areas of the page containing the point, smallest (topmost) first.
    QList<qint64> areasAt(int page, const QPointF &pagePos) const;

    bool hasSelection() const { return m_sel.page > 0; }
    Selection selection() const { return m_sel; }
    // Selects a rectangle; `text` is what it covers (taken from the page if not given).
    void setSelection(int page, const QRectF &rect, const QString &text = {});
    void clearSelection();

    // Page and page point under a point of the viewport; false if it is not over a page.
    bool pageAt(const QPoint &viewportPos, int *page, QPointF *pagePos) const;
    QPoint toViewport(int page, const QPointF &pagePos) const;
    QRect pageRect(int page) const; // in viewport coordinates

    int currentPage() const { return m_currentPage; }
    void goToPage(int page);
    void scrollToRect(int page, const QRectF &rect);

    double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    void fitWidth();
    void actualSize() { setZoom(1.0); }
    bool isFitWidth() const { return m_fitWidth; }

signals:
    void selectionChanged();
    void areaClicked(const QList<qint64> &ids); // topmost first
    void areaContextMenu(const QList<qint64> &ids, const QPoint &globalPos);
    void codeDropped(qint64 codeId, int page, const QPointF &pagePos);
    void zoomChanged();
    void currentPageChanged(int page);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    struct Bar { // a code bar in the margin of a page
        qint64 id;
        QColor color;
        QRectF rect; // content coordinates
    };
    double pxPerPt() const;
    void updateLayout(bool keepPosition = true);
    void layoutBars();
    double pageLeft(int page) const; // content x of the page
    QRectF pageContentRect(int page) const;
    QPointF clampedPagePos(int page, const QPoint &viewportPos) const; // the point moved onto the page
    QImage pageImage(int page);
    QList<qint64> barsAt(const QPoint &viewportPos) const;
    void updateCurrentPage();
    void applyZoom(double zoom, bool keepPosition);
    void finishSelection();

    std::unique_ptr<Pdfs::Doc> m_doc;
    QList<QSizeF> m_pageSizes; // points
    QList<double> m_pageTops;  // content y of each page, in pixels
    QSizeF m_content;
    QList<Area> m_areas;
    QList<Bar> m_bars;
    qint64 m_highlighted = 0;
    Selection m_sel;
    QList<QPolygonF> m_selBounds; // text selection, points of m_sel.page
    double m_zoom = 1.0;
    bool m_fitWidth = true;
    int m_currentPage = 1;

    QHash<quint64, QImage> m_cache;
    QList<quint64> m_cacheOrder;

    bool m_pressed = false;
    bool m_dragging = false;
    bool m_alt = false;
    bool m_panning = false;
    int m_pressPage = 0;
    QPoint m_pressPos;
    QPoint m_lastPanPos;
    QPointF m_pressPagePos;
};
