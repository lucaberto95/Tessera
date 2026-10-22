#pragma once

#include "core/Exporters.h"
#include "core/Stats.h"

#include <QDialog>

class Project;
class QChartView;
class QComboBox;
class QLabel;
class QSpinBox;
class QTableWidget;

// Descriptive statistics of the document variables and the code x group cross tabulation.
class StatsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Columns { Variable, DocumentGroups, DocumentSets };

    StatsDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                QWidget *parent = nullptr);

    void selectVariable(qint64 variableId); // "Variabili" tab
    const Stats::VariableStats &variableStats() const { return m_stats; }

    // "Tabella incrociata" tab: columns come from a variable (variableId, numeric ones in `bins` classes
    // when bins > 0), the document groups or the document sets.
    void setColumns(Columns columns, qint64 variableId = 0, int bins = 0);
    void setMeasure(Stats::Measure measure);
    void setPercent(Stats::Percent percent);
    const Stats::Crosstab &crosstab() const { return m_crosstab; }
    const Stats::ChiSquare &chiSquare() const { return m_chi; }
    // Bars of the chart shown for the selected variable (frequencies, or histogram classes).
    int chartBars() const { return m_chartBars; }

private:
    void fillVariables();
    void rebuildChart();
    void rebuildStats();
    void rebuildCrosstab();
    void exportTable(const Exporters::Table &table, const QString &suffix);

    Project *m_project;
    QList<qint64> m_docIds;
    QList<qint64> m_codeIds;
    Stats::VariableStats m_stats;
    Stats::Crosstab m_crosstab;
    Stats::ChiSquare m_chi;
    int m_chartBars = 0;
    Exporters::Table m_statsExport;
    Exporters::Table m_crosstabExport;

    QComboBox *m_variable;
    QTableWidget *m_statsTable;
    QLabel *m_summary;
    QChartView *m_chart;
    QLabel *m_chiSquare;

    QComboBox *m_columns;
    QComboBox *m_columnVariable;
    QSpinBox *m_bins;
    QComboBox *m_measure;
    QComboBox *m_percent;
    QTableWidget *m_crosstabTable;
};
