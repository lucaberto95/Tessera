#pragma once

#include "core/Cooccurrence.h"
#include "core/Exporters.h"

#include <QDialog>

class Project;
class QComboBox;
class QDoubleSpinBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QSpinBox;
class QTableWidget;

// Code × code co-occurrence matrix (heatmap) and a force-directed map of the same data.
class CooccurrenceDialog : public QDialog {
    Q_OBJECT
public:
    CooccurrenceDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                       QWidget *parent = nullptr);

    const Cooccurrence::Matrix &matrix() const { return m_matrix; }
    void setCriterion(Cooccurrence::Criterion criterion);
    void setNormalized(bool jaccard);
    // Map: edges below the threshold are hidden. Returns the numbers of nodes and visible edges.
    void setMapThreshold(double threshold);
    int mapNodeCount() const { return m_mapNodes; }
    int mapEdgeCount() const { return m_mapEdges; }

signals:
    // Passages behind the activated cell, ready for the segments panel.
    void passagesRequested(const QList<Retrieval::Result> &passages, const QString &label);

private:
    Cooccurrence::Options options() const;
    double cellValue(qint64 a, qint64 b) const;
    QString cellText(double value, bool frequency = false) const; // frequencies are always whole numbers
    void rebuild();
    void rebuildMatrix();
    void rebuildMap();
    void activateCell(int row, int col);
    void exportMatrix();
    void exportMapPng();
    void exportMapSvg();

    Project *m_project;
    QList<qint64> m_docIds;
    QList<qint64> m_codeIds;
    QList<Segment> m_segments;
    Cooccurrence::Matrix m_matrix;
    QComboBox *m_criterion;
    QSpinBox *m_distance;
    QComboBox *m_unit;
    QComboBox *m_value;
    QDoubleSpinBox *m_threshold;
    QLabel *m_scopeLabel;
    QTableWidget *m_table;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    Exporters::Table m_exportTable;
    int m_mapNodes = 0;
    int m_mapEdges = 0;
};
