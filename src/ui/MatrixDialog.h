#pragma once

#include "core/Exporters.h"

#include <QDialog>

class Project;
class QCheckBox;
class QChartView;
class QLabel;
class QTableWidget;

// Code × document matrix, code frequencies and a frequency chart.
class MatrixDialog : public QDialog {
    Q_OBJECT
public:
    MatrixDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                 QWidget *parent = nullptr);

signals:
    void cellActivated(qint64 codeId, qint64 documentId);

private:
    void rebuild();
    void exportTable(const Exporters::Table &table, const QString &suffix,
                     const QMap<std::pair<int, int>, QColor> &fills);

    Project *m_project;
    QList<qint64> m_docIds;
    QList<qint64> m_codeIds;
    QCheckBox *m_presenceOnly;
    QCheckBox *m_aggregate;
    QTableWidget *m_matrix;
    QTableWidget *m_frequencies;
    QChartView *m_chart;
    QLabel *m_scopeLabel;
    Exporters::Table m_matrixTable;
    QMap<std::pair<int, int>, QColor> m_matrixFills; // the heatmap and the code colours, for the .xlsx export
    Exporters::Table m_frequencyTable;
    QMap<std::pair<int, int>, QColor> m_frequencyFills;
};
