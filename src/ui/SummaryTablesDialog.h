#pragma once

#include "core/Exporters.h"
#include "core/Summaries.h"

#include <QDialog>

class Project;
class QComboBox;
class QListWidget;
class QRadioButton;
class QTableWidget;

// Saved summary tables (issue #14): documents (or a set) as rows, codes as columns, optionally the
// values of variables; each cell holds the summary written in the grid. The table is generated again
// whenever summaries change, and can be exported to Excel, CSV or HTML.
class SummaryTablesDialog : public QDialog {
    Q_OBJECT
public:
    SummaryTablesDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                        QWidget *parent = nullptr);

    qint64 currentTableId() const;
    void selectTable(qint64 id);
    Exporters::Table currentTable() const { return m_table; }
    // Test hooks: the choices of the current table, applied and saved as if made in the dialog.
    void setConfig(const Summaries::TableConfig &config);
    Summaries::TableConfig config() const;
    qint64 createTable(const QString &name); // from the active documents and codes
    QTableWidget *preview() const { return m_preview; }

private:
    void reloadList();
    void loadConfig();
    void configChanged();
    void refreshPreview();
    void newTable();
    void renameTable();
    void deleteTable();
    void exportTable();
    void setChecks(QListWidget *list, const QList<qint64> &ids);
    static QList<qint64> checkedIds(const QListWidget *list);

    Project *m_project;
    QList<qint64> m_activeDocs;
    QList<qint64> m_activeCodes;
    QListWidget *m_tables;
    QRadioButton *m_byDocuments;
    QRadioButton *m_bySet;
    QComboBox *m_sets;
    QListWidget *m_documents;
    QListWidget *m_codes;
    QListWidget *m_variables;
    QTableWidget *m_preview;
    Exporters::Table m_table;
    bool m_loading = false;
};
