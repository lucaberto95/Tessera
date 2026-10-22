#include "ui/PdfView.h"
#include "ui/UiUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double MinZoom = 0.1;
constexpr double MaxZoom = 8.0;
constexpr double ScreenDpi = 96.0; // zoom 100% = the page at its printed size on a 96 dpi screen
constexpr double MarginX = 44;     // beside the pages: room for the code bars
constexpr double MarginY = 12;
constexpr double BarWidth = 5, BarStep = 7;
constexpr int MaxBarColumns = 5;
constexpr int ClickTolerance = 4;   // pixels of the window: a smaller movement is a click, not a drag
constexpr int MaxCachedPages = 12;
} // namespace

PdfView::PdfView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    viewport()->setBackgroundRole(QPalette::Dark);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    viewport()->setCursor(Qt::IBeamCursor);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn); // keeps the width stable while fitting to it
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    verticalScrollBar()->setSingleStep(48);
    horizontalScrollBar()->setSingleStep(48);
}

double PdfView::pxPerPt() const
{
    return m_zoom * ScreenDpi / 72.0;
}

bool PdfView::setPdf(const QByteArray &data)
{
    clear();
    auto doc = std::make_unique<Pdfs::Doc>();
    if (!doc->load(data))
        return false;
    m_doc = std::move(doc);
    for (int i = 1; i <= m_doc->pageCount(); ++i)
        m_pageSizes << m_doc->pageSize(i);
    m_fitWidth = true;
    updateLayout(false);
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    return true;
}

void PdfView::clear()
{
    m_doc.reset();
    m_pageSizes.clear();
    m_pageTops.clear();
    m_areas.clear();
    m_bars.clear();
    m_cache.clear();
    m_cacheOrder.clear();
    m_highlighted = 0;
    m_sel = {};
    m_selBounds.clear();
    m_content = {};
    m_currentPage = 1;
    m_pressed = m_dragging = m_panning = false;
    horizontalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setRange(0, 0);
    viewport()->update();
}

// ---------------------------------------------------------------- layout

double PdfView::pageLeft(int page) const
{
    const double w = m_pageSizes.value(page - 1).width() * pxPerPt();
    const double avail = std::max<double>(viewport()->width(), m_content.width());
    return (avail - w) / 2.0;
}

QRectF PdfView::pageContentRect(int page) const
{
    const QSizeF s = m_pageSizes.value(page - 1);
    return QRectF(pageLeft(page), m_pageTops.value(page - 1), s.width() * pxPerPt(), s.height() * pxPerPt());
}

QRect PdfView::pageRect(int page) const
{
    if (page < 1 || page > m_pageSizes.size())
        return {};
    return pageContentRect(page)
        .translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value())
        .toAlignedRect();
}

void PdfView::updateLayout(bool keepPosition)
{
    if (!hasPdf()) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
        return;
    }
    const double vw = viewport()->width(), vh = viewport()->height();
    const double fx = m_content.width() > 0 ? (horizontalScrollBar()->value() + vw / 2) / m_content.width() : 0.5;
    const double fy = m_content.height() > 0 ? (verticalScrollBar()->value() + vh / 2) / m_content.height() : 0.0;

    double maxWidthPt = 1;
    for (const QSizeF &s : std::as_const(m_pageSizes))
        maxWidthPt = std::max(maxWidthPt, s.width());
    if (m_fitWidth && vw > 2 * MarginX + 20) {
        const double z = std::clamp((vw - 2 * MarginX) / maxWidthPt / (ScreenDpi / 72.0), MinZoom, MaxZoom);
        if (std::abs(z - m_zoom) > 1e-9) {
            m_zoom = z;
            emit zoomChanged();
        }
    }

    const double ppp = pxPerPt();
    m_pageTops.clear();
    double y = MarginY;
    for (const QSizeF &s : std::as_const(m_pageSizes)) {
        m_pageTops << y;
        y += s.height() * ppp + MarginY;
    }
    m_content = QSizeF(maxWidthPt * ppp + 2 * MarginX, y);
    horizontalScrollBar()->setRange(0, std::max(0, int(std::ceil(m_content.width() - vw))));
    horizontalScrollBar()->setPageStep(int(vw));
    verticalScrollBar()->setRange(0, std::max(0, int(std::ceil(m_content.height() - vh))));
    verticalScrollBar()->setPageStep(int(vh));
    if (keepPosition) {
        horizontalScrollBar()->setValue(int(fx * m_content.width() - vw / 2));
        verticalScrollBar()->setValue(int(fy * m_content.height() - vh / 2));
    }
    layoutBars();
    updateCurrentPage();
    viewport()->update();
}

// Each area gets a bar in the margin of its page; areas that overlap vertically take further columns.
void PdfView::layoutBars()
{
    m_bars.clear();
    const double ppp = pxPerPt();
    for (int page = 1; page <= m_pageSizes.size(); ++page) {
        QList<const Area *> onPage;
        for (const Area &a : std::as_const(m_areas))
            if (a.page == page)
                onPage << &a;
        std::stable_sort(onPage.begin(), onPage.end(), [](const Area *a, const Area *b) { return a->rect.top() < b->rect.top(); });
        QList<double> columnEnd;
        const double left = pageLeft(page), top = m_pageTops.value(page - 1);
        for (const Area *a : std::as_const(onPage)) {
            int col = 0;
            while (col < columnEnd.size() && columnEnd[col] > a->rect.top())
                ++col;
            if (col == columnEnd.size())
                columnEnd << 0;
            columnEnd[col] = a->rect.bottom();
            const int drawn = std::min(col, MaxBarColumns - 1);
            const double x = left - 8 - (drawn + 1) * BarStep + (BarStep - BarWidth);
            m_bars.append({a->id, a->color.isValid() ? a->color : QColor(Qt::gray),
                           QRectF(x, top + a->rect.top() * ppp, BarWidth, std::max(4.0, a->rect.height() * ppp))});
        }
    }
}

void PdfView::updateCurrentPage()
{
    int page = 1;
    const double y = verticalScrollBar()->value() + viewport()->height() / 3.0;
    for (int i = 0; i < m_pageTops.size(); ++i)
        if (m_pageTops[i] <= y)
            page = i + 1;
    if (page != m_currentPage) {
        m_currentPage = page;
        emit currentPageChanged(page);
    }
}

void PdfView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateLayout(true);
}

void PdfView::scrollContentsBy(int, int)
{
    viewport()->update();
    updateCurrentPage();
}

void PdfView::applyZoom(double zoom, bool keepPosition)
{
    zoom = std::clamp(zoom, MinZoom, MaxZoom);
    if (std::abs(zoom - m_zoom) < 1e-9 && !m_fitWidth)
        return;
    m_zoom = zoom;
    updateLayout(keepPosition);
    emit zoomChanged();
}

void PdfView::setZoom(double zoom)
{
    m_fitWidth = false;
    applyZoom(zoom, true);
}

void PdfView::fitWidth()
{
    m_fitWidth = true;
    updateLayout(true);
}

void PdfView::goToPage(int page)
{
    if (page < 1 || page > m_pageTops.size())
        return;
    verticalScrollBar()->setValue(int(m_pageTops[page - 1] - MarginY));
}

void PdfView::scrollToRect(int page, const QRectF &rect)
{
    if (page < 1 || page > m_pageTops.size())
        return;
    const double ppp = pxPerPt();
    const QRectF r(pageLeft(page) + rect.left() * ppp, m_pageTops[page - 1] + rect.top() * ppp, rect.width() * ppp,
                   rect.height() * ppp);
    const int vh = viewport()->height(), vw = viewport()->width();
    QScrollBar *v = verticalScrollBar(), *h = horizontalScrollBar();
    if (r.top() < v->value() || r.bottom() > v->value() + vh)
        v->setValue(int(r.top() - vh / 4.0));
    if (r.left() < h->value() || r.right() > h->value() + vw)
        h->setValue(int(r.center().x() - vw / 2.0));
}

// ---------------------------------------------------------------- geometry of points

bool PdfView::pageAt(const QPoint &viewportPos, int *page, QPointF *pagePos) const
{
    const QPointF c(viewportPos.x() + horizontalScrollBar()->value(), viewportPos.y() + verticalScrollBar()->value());
    for (int i = 1; i <= m_pageSizes.size(); ++i) {
        const QRectF r = pageContentRect(i);
        if (r.contains(c)) {
            if (page)
                *page = i;
            if (pagePos)
                *pagePos = (c - r.topLeft()) / pxPerPt();
            return true;
        }
    }
    return false;
}

QPoint PdfView::toViewport(int page, const QPointF &pagePos) const
{
    const QRectF r = pageContentRect(page);
    return QPoint(int(std::lround(r.left() + pagePos.x() * pxPerPt())) - horizontalScrollBar()->value(),
                  int(std::lround(r.top() + pagePos.y() * pxPerPt())) - verticalScrollBar()->value());
}

QPointF PdfView::clampedPagePos(int page, const QPoint &viewportPos) const
{
    const QRectF r = pageContentRect(page);
    const QPointF c(viewportPos.x() + horizontalScrollBar()->value(), viewportPos.y() + verticalScrollBar()->value());
    const QSizeF s = m_pageSizes.value(page - 1);
    return {std::clamp((c.x() - r.left()) / pxPerPt(), 0.0, s.width()),
            std::clamp((c.y() - r.top()) / pxPerPt(), 0.0, s.height())};
}

QList<qint64> PdfView::areasAt(int page, const QPointF &p) const
{
    QList<const Area *> hits;
    for (const Area &a : m_areas)
        if (a.page == page && a.rect.contains(p))
            hits << &a;
    std::stable_sort(hits.begin(), hits.end(), [](const Area *x, const Area *y) {
        return x->rect.width() * x->rect.height() < y->rect.width() * y->rect.height();
    });
    QList<qint64> ids;
    for (const Area *a : hits)
        ids << a->id;
    return ids;
}

QList<qint64> PdfView::barsAt(const QPoint &viewportPos) const
{
    const QPointF c(viewportPos.x() + horizontalScrollBar()->value(), viewportPos.y() + verticalScrollBar()->value());
    QList<qint64> ids;
    for (const Bar &b : m_bars)
        if (b.rect.adjusted(-1, 0, 1, 0).contains(c))
            ids << b.id;
    return ids;
}

// ---------------------------------------------------------------- areas and selection

void PdfView::setAreas(const QList<Area> &areas)
{
    m_areas = areas;
    bool found = false;
    for (const Area &a : std::as_const(m_areas))
        found = found || a.id == m_highlighted;
    if (!found)
        m_highlighted = 0;
    layoutBars();
    viewport()->update();
}

void PdfView::highlightArea(qint64 id, bool scrollTo)
{
    m_highlighted = 0;
    for (const Area &a : std::as_const(m_areas)) {
        if (a.id == id) {
            m_highlighted = id;
            if (scrollTo)
                scrollToRect(a.page, a.rect);
        }
    }
    viewport()->update();
}

void PdfView::setSelection(int page, const QRectF &rect, const QString &text)
{
    Selection s;
    if (page >= 1 && page <= m_pageSizes.size() && !rect.isNull()) {
        const QRectF r = rect.normalized().intersected(QRectF(QPointF(0, 0), m_pageSizes[page - 1]));
        if (r.width() >= 1 && r.height() >= 1) {
            s.page = page;
            s.rect = r;
            s.text = text.isNull() && m_doc ? m_doc->textInArea(page, r) : text;
        }
    }
    m_selBounds.clear();
    const bool same = s.page == m_sel.page && s.rect == m_sel.rect && s.isText == m_sel.isText && s.text == m_sel.text;
    m_sel = s;
    viewport()->update();
    if (!same)
        emit selectionChanged();
}

void PdfView::clearSelection()
{
    setSelection(0, {});
}

// ---------------------------------------------------------------- painting

QImage PdfView::pageImage(int page)
{
    const double dpr = devicePixelRatioF();
    const QSizeF s = m_pageSizes.value(page - 1);
    const QSize px(std::max(1, int(std::lround(s.width() * pxPerPt() * dpr))),
                   std::max(1, int(std::lround(s.height() * pxPerPt() * dpr))));
    const quint64 key = (quint64(page) << 32) | quint64(px.width());
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd())
        return it.value();
    QImage image = m_doc->render(page, px);
    if (m_cacheOrder.size() >= MaxCachedPages) {
        m_cache.remove(m_cacheOrder.takeFirst());
    }
    m_cache.insert(key, image);
    m_cacheOrder << key;
    return image;
}

void PdfView::paintEvent(QPaintEvent *event)
{
    QPainter p(viewport());
    p.fillRect(event->rect(), QColor(0x4a4a4a));
    if (!hasPdf())
        return;
    const QPointF off(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    const double ppp = pxPerPt();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    for (int page = 1; page <= m_pageSizes.size(); ++page) {
        const QRectF pr = pageContentRect(page).translated(off);
        if (!pr.intersects(event->rect()))
            continue;
        p.fillRect(pr, Qt::white);
        const QImage img = pageImage(page);
        if (!img.isNull())
            p.drawImage(pr, img);
        p.setPen(QColor(0x2b2b2b));
        p.drawRect(pr.adjusted(-0.5, -0.5, 0.5, 0.5));

        auto toWidget = [&](const QRectF &r) {
            return QRectF(pr.left() + r.left() * ppp, pr.top() + r.top() * ppp, r.width() * ppp, r.height() * ppp);
        };
        // Larger areas first, so a nested area stays visible over the one around it.
        QList<const Area *> onPage;
        for (const Area &a : std::as_const(m_areas))
            if (a.page == page)
                onPage << &a;
        std::stable_sort(onPage.begin(), onPage.end(), [](const Area *x, const Area *y) {
            return x->rect.width() * x->rect.height() > y->rect.width() * y->rect.height();
        });
        for (const Area *a : std::as_const(onPage)) {
            QColor color = a->color.isValid() ? a->color : QColor(Qt::gray);
            const bool on = a->id == m_highlighted;
            QColor fill = color;
            fill.setAlpha(on ? 110 : 55);
            p.setPen(QPen(color, on ? 2.5 : 1.2));
            p.setBrush(fill);
            p.drawRect(toWidget(a->rect));
        }
        if (m_sel.page == page) {
            if (m_sel.isText && !m_selBounds.isEmpty()) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(21, 101, 192, 80));
                for (const QPolygonF &poly : std::as_const(m_selBounds)) {
                    QPolygonF mapped;
                    for (const QPointF &pt : poly)
                        mapped << QPointF(pr.left() + pt.x() * ppp, pr.top() + pt.y() * ppp);
                    p.drawPolygon(mapped);
                }
            } else {
                p.setPen(QPen(QColor(0x1565c0), 2, Qt::DashLine));
                p.setBrush(QColor(21, 101, 192, 40));
                p.drawRect(toWidget(m_sel.rect));
            }
        }
    }
    // Code bars in the margins.
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const Bar &b : std::as_const(m_bars)) {
        const QRectF r = b.rect.translated(off);
        if (!r.intersects(event->rect()))
            continue;
        p.fillRect(r, b.color);
        if (b.id == m_highlighted) {
            p.setPen(QPen(Qt::white, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(-1, -1, 1, 1));
        }
    }
}

// ---------------------------------------------------------------- mouse

void PdfView::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier) || !hasPdf() || event->angleDelta().y() == 0) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }
    m_fitWidth = false;
    applyZoom(m_zoom * std::pow(1.0015, event->angleDelta().y()), true);
    event->accept();
}

void PdfView::mousePressEvent(QMouseEvent *event)
{
    if (!hasPdf()) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus();
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_dragging = false;
        m_alt = event->modifiers() & Qt::AltModifier;
        m_pressPos = event->position().toPoint();
        m_pressPage = 0;
        if (pageAt(m_pressPos, &m_pressPage, &m_pressPagePos)) {
            // fine: the drag starts on a page
        }
        event->accept();
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = event->position().toPoint();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QAbstractScrollArea::mousePressEvent(event);
    }
}

void PdfView::mouseMoveEvent(QMouseEvent *event)
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
        if (m_dragging && m_pressPage) {
            const QPointF cur = clampedPagePos(m_pressPage, pos);
            if (m_alt) {
                m_sel = {m_pressPage, QRectF(m_pressPagePos, cur).normalized(), {}, false};
                m_selBounds.clear();
            } else {
                const Pdfs::Doc::Selection s = m_doc->select(m_pressPage, m_pressPagePos, cur);
                m_sel = {s.isEmpty() ? 0 : m_pressPage, s.rect, s.text, true};
                m_selBounds = s.bounds;
            }
            viewport()->update();
        }
        event->accept();
        return;
    }
    viewport()->setCursor(Qt::IBeamCursor);
    QAbstractScrollArea::mouseMoveEvent(event);
}

void PdfView::finishSelection()
{
    if (m_sel.page > 0 && !m_sel.isText) {
        if (m_sel.rect.width() < 2 || m_sel.rect.height() < 2) {
            m_sel = {};
        } else {
            m_sel.rect = m_sel.rect.intersected(QRectF(QPointF(0, 0), m_pageSizes.value(m_sel.page - 1)));
            m_sel.text = m_doc->textInArea(m_sel.page, m_sel.rect);
        }
    }
    viewport()->update();
    emit selectionChanged();
}

void PdfView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        viewport()->setCursor(Qt::IBeamCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        if (m_dragging) {
            m_dragging = false;
            finishSelection();
        } else {
            QList<qint64> ids = barsAt(m_pressPos);
            if (ids.isEmpty() && m_pressPage)
                ids = areasAt(m_pressPage, m_pressPagePos);
            if (ids.isEmpty())
                clearSelection();
            else
                emit areaClicked(ids);
        }
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void PdfView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!hasPdf())
        return;
    QList<qint64> ids = barsAt(event->pos());
    int page = 0;
    QPointF pt;
    if (ids.isEmpty() && pageAt(event->pos(), &page, &pt))
        ids = areasAt(page, pt);
    emit areaContextMenu(ids, event->globalPos());
}

void PdfView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy) && m_sel.page > 0 && !m_sel.text.isEmpty()) {
        QApplication::clipboard()->setText(m_sel.text);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_sel.page > 0) {
        clearSelection();
        event->accept();
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

bool PdfView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        QList<qint64> ids = barsAt(help->pos());
        int page = 0;
        QPointF pt;
        if (ids.isEmpty() && pageAt(help->pos(), &page, &pt))
            ids = areasAt(page, pt);
        QStringList tips;
        for (qint64 id : std::as_const(ids))
            for (const Area &a : std::as_const(m_areas))
                if (a.id == id)
                    tips << a.tip;
        if (tips.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), Ui::plainToolTip(tips.join(QLatin1Char('\n'))), viewport());
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void PdfView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void PdfView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void PdfView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    int page = 0;
    QPointF pt;
    if (!pageAt(event->position().toPoint(), &page, &pt))
        page = 0;
    emit codeDropped(event->mimeData()->data(QString::fromLatin1(Ui::CodeMimeType)).toLongLong(), page, pt);
}
