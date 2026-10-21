#pragma once

#include "core/Model.h"

#include <QHash>
#include <QWidget>

class QTextEdit;

// Margin next to the document text showing one bracket per coded segment, in the code's color,
// with the code name written along it. Overlapping segments are laid out in separate columns.
class CodingStripe : public QWidget {
    Q_OBJECT
public:
    CodingStripe(QTextEdit *edit, QWidget *parent = nullptr);

    void setSegments(const QList<Segment> &segments, const QHash<qint64, Code> &codes);
    void setHighlightedSegment(qint64 id);
    QSize sizeHint() const override;

signals:
    void segmentClicked(qint64 segmentId);
    void segmentContextMenu(qint64 segmentId, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Bar {
        qint64 segmentId;
        int column;
        QRect rect; // in widget coordinates
    };
    // Recomputes the vertical extent of every segment (document coordinates) and assigns columns
    // so that brackets overlapping on screen never share a column. Needed after text re-wrapping.
    void relayout();
    QList<Bar> computeBars() const;
    qint64 segmentAt(const QPoint &pos) const;

    QTextEdit *m_edit;
    QList<Segment> m_segments;
    QHash<qint64, Code> m_codes;
    QHash<qint64, int> m_columns;
    QHash<qint64, std::pair<int, int>> m_extents; // segment -> (top, bottom) in document pixels
    int m_columnCount = 0;
    qint64 m_highlighted = 0;
};
