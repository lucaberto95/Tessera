#include "ui/CodingStripe.h"

#include <QAbstractTextDocumentLayout>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolTip>

namespace {
constexpr int ColumnWidth = 18;
constexpr int Margin = 4;
} // namespace

CodingStripe::CodingStripe(QTextEdit *edit, QWidget *parent)
    : QWidget(parent)
    , m_edit(edit)
{
    setMouseTracking(true);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Base);
    connect(edit->verticalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    // Re-wrapping (window resize, zoom, editing) moves the brackets.
    edit->viewport()->installEventFilter(this);
    connect(edit->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            &CodingStripe::relayout);
    relayout();
}

void CodingStripe::setSegments(const QList<Segment> &segments, const QHash<qint64, Code> &codes)
{
    m_segments = segments;
    m_codes = codes;
    relayout();
}

bool CodingStripe::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit->viewport() && event->type() == QEvent::Resize)
        QMetaObject::invokeMethod(this, &CodingStripe::relayout, Qt::QueuedConnection); // after re-wrapping
    return QWidget::eventFilter(watched, event);
}

void CodingStripe::setHighlightedSegment(qint64 id)
{
    m_highlighted = id;
    update();
}

QSize CodingStripe::sizeHint() const
{
    return {Margin * 2 + qMax(1, m_columnCount) * ColumnWidth, 100};
}

void CodingStripe::relayout()
{
    m_extents.clear();
    m_columns.clear();
    const int docLength = m_edit->document()->characterCount() - 1;
    const int scroll = m_edit->verticalScrollBar()->value();
    for (const Segment &s : m_segments) {
        if (s.start > docLength)
            continue;
        QTextCursor c(m_edit->document());
        c.setPosition(qBound(0, s.start, docLength));
        const QRect top = m_edit->cursorRect(c);
        c.setPosition(qBound(0, s.end, docLength));
        const QRect bottom = m_edit->cursorRect(c);
        const int y0 = top.top() + scroll;
        m_extents.insert(s.id, {y0, y0 + qMax(bottom.bottom() - top.top(), 8)});
    }

    // Greedy interval colouring on the on-screen extents (plus a small gap), longest first at equal top.
    QList<Segment> sorted = m_segments;
    std::sort(sorted.begin(), sorted.end(), [this](const Segment &a, const Segment &b) {
        const auto ea = m_extents.value(a.id), eb = m_extents.value(b.id);
        return ea.first != eb.first ? ea.first < eb.first : ea.second > eb.second;
    });
    QList<int> columnBottoms;
    for (const Segment &s : sorted) {
        if (!m_extents.contains(s.id))
            continue;
        const auto [top, bottom] = m_extents.value(s.id);
        int col = 0;
        while (col < columnBottoms.size() && columnBottoms[col] + 3 > top)
            ++col;
        if (col == columnBottoms.size())
            columnBottoms << bottom;
        else
            columnBottoms[col] = bottom;
        m_columns.insert(s.id, col);
    }
    if (m_columnCount != columnBottoms.size()) {
        m_columnCount = int(columnBottoms.size());
        setFixedWidth(sizeHint().width());
    }
    update();
}

QList<CodingStripe::Bar> CodingStripe::computeBars() const
{
    QList<Bar> bars;
    // Offset between the text edit's viewport and this widget, and the current scroll position.
    const int dy = m_edit->viewport()->mapTo(window(), QPoint(0, 0)).y() - mapTo(window(), QPoint(0, 0)).y() -
                   m_edit->verticalScrollBar()->value();
    for (const Segment &s : m_segments) {
        if (!m_extents.contains(s.id))
            continue;
        const auto [top, bottom] = m_extents.value(s.id);
        const int col = m_columns.value(s.id);
        // Columns grow leftwards from the text so nested codes sit closest to it.
        const int x = width() - Margin - (col + 1) * ColumnWidth;
        bars.append({s.id, col, QRect(x, top + dy, ColumnWidth - 3, bottom - top)});
    }
    return bars;
}

void CodingStripe::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(width() - 1, 0, width() - 1, height());

    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    const QFontMetrics fm(f);

    for (const Bar &bar : computeBars()) {
        if (bar.rect.bottom() < 0 || bar.rect.top() > height())
            continue;
        const Segment *seg = nullptr;
        for (const Segment &s : m_segments)
            if (s.id == bar.segmentId)
                seg = &s;
        const Code code = m_codes.value(seg->codeId);
        QColor color = code.color.isValid() ? code.color : QColor(Qt::gray);
        const bool hl = bar.segmentId == m_highlighted;
        const QRect r = bar.rect;

        // Bracket: vertical line on the right of the column, ticks pointing to the text.
        QPen pen(color, hl ? 3.0 : 2.0);
        p.setPen(pen);
        const int lineX = r.right() - 1;
        p.drawLine(lineX, r.top(), lineX, r.bottom());
        p.drawLine(lineX, r.top(), r.right() + 2, r.top());
        p.drawLine(lineX, r.bottom(), r.right() + 2, r.bottom());

        // Code name rotated along the bracket.
        if (r.height() > 24) {
            p.save();
            // After rotating, the baseline runs downwards and glyph tops face the bracket line.
            p.translate(r.left() + fm.descent(), r.top() + 3);
            p.rotate(90);
            p.setPen(hl ? color.darker(150) : color.darker(120));
            p.drawText(QPoint(0, 0), fm.elidedText(code.name, Qt::ElideRight, r.height() - 6));
            p.restore();
        }
    }
}

qint64 CodingStripe::segmentAt(const QPoint &pos) const
{
    qint64 best = 0;
    int bestHeight = INT_MAX;
    for (const Bar &bar : computeBars()) {
        if (bar.rect.adjusted(-1, -2, 3, 2).contains(pos) && bar.rect.height() < bestHeight) {
            best = bar.segmentId;
            bestHeight = bar.rect.height();
        }
    }
    return best;
}

void CodingStripe::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (const qint64 id = segmentAt(event->position().toPoint()))
            emit segmentClicked(id);
    }
}

void CodingStripe::contextMenuEvent(QContextMenuEvent *event)
{
    if (const qint64 id = segmentAt(event->pos()))
        emit segmentContextMenu(id, event->globalPos());
}

bool CodingStripe::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const qint64 id = segmentAt(help->pos());
        for (const Segment &s : m_segments) {
            if (s.id != id)
                continue;
            QString tip = QStringLiteral("<b>%1</b>").arg(m_codes.value(s.codeId).name.toHtmlEscaped());
            if (s.weight)
                tip += QStringLiteral("<br>") + tr("Peso: %1").arg(s.weight);
            if (!s.comment.isEmpty())
                tip += QStringLiteral("<br><i>%1</i>").arg(s.comment.toHtmlEscaped());
            tip += QStringLiteral("<br><span style='color:gray'>%1</span>").arg(s.author.toHtmlEscaped());
            QToolTip::showText(help->globalPos(), tip, this);
            return true;
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}
