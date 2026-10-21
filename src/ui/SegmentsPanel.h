#pragma once

#include "core/Model.h"

#include <QWidget>

class Project;
class QLabel;
class QLineEdit;
class QTableWidget;

// "Retrieved segments": all segments of the active codes in the active documents.
class SegmentsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SegmentsPanel(Project *project, QWidget *parent = nullptr);

    void setActivation(const QList<qint64> &documentIds, const QList<qint64> &codeIds);
    void refresh();
    QList<Segment> segments() const { return m_segments; }
    void exportHtml();
    void exportCsv();

signals:
    void segmentActivated(qint64 documentId, int start, int end, qint64 segmentId);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void applyFilter();
    qint64 segmentIdAtRow(int row) const;

    Project *m_project;
    QList<qint64> m_documentIds;
    QList<qint64> m_codeIds;
    QList<Segment> m_segments;
    QLabel *m_summary;
    QLineEdit *m_filter;
    QTableWidget *m_table;
};
