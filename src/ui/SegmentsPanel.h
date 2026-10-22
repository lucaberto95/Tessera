#pragma once

#include "core/Model.h"
#include "core/Retrieval.h"

#include <QWidget>

class Project;
class QLabel;
class QLineEdit;
class QToolButton;
class QTableWidget;

// "Retrieved segments": all segments of the active codes in the active documents.
class SegmentsPanel : public QWidget {
    Q_OBJECT
public:
    explicit SegmentsPanel(Project *project, QWidget *parent = nullptr);

    void setActivation(const QList<qint64> &documentIds, const QList<qint64> &codeIds);
    void refresh();
    QList<Segment> segments() const { return m_segments; }
    // Complex retrieval: shows the result of the query (re-run on every refresh) instead of the simple union.
    void setRetrieval(const Retrieval::Query &query);
    // Shows ready-made passages (e.g. a co-occurrence cell) as a retrieval; `label` names their origin.
    void setPassages(const QList<Retrieval::Result> &passages, const QString &label);
    void clearRetrieval();
    bool hasRetrieval() const { return m_hasQuery; }
    int resultCount() const { return int(m_segments.size()); }
    // Codes every retrieved passage with a new top-level code; returns its id (0 if nothing was done).
    qint64 codeResults(const QString &codeName);
    void exportHtml();
    void exportCsv();
    void exportXlsx();

signals:
    void segmentActivated(qint64 documentId, int start, int end, qint64 segmentId);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void applyFilter();
    void rebuildSegmentsFromQuery();
    void codeResultsInteractive();

    Project *m_project;
    QList<qint64> m_documentIds;
    QList<qint64> m_codeIds;
    QList<Segment> m_segments;
    Retrieval::Query m_query;
    bool m_hasQuery = false;
    bool m_fixed = false; // m_results come from setPassages(), not from running m_query
    QList<Retrieval::Result> m_fixedResults;
    QString m_fixedLabel;
    QList<Retrieval::Result> m_results;
    QToolButton *m_back;
    QToolButton *m_codeResults;
    QLabel *m_summary;
    QLineEdit *m_filter;
    QTableWidget *m_table;
};
