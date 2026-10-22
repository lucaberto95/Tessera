#pragma once

#include <QDialog>

class Project;
class QTableWidget;
class QTableWidgetItem;

// Spreadsheet-like editor for document variables (one row per document, one column per variable).
class VariablesDialog : public QDialog {
    Q_OBJECT
public:
    explicit VariablesDialog(Project *project, QWidget *parent = nullptr);

private:
    void refresh();
    void addVariable();
    void renameVariable();
    void deleteVariable();
    void importCsv();
    void exportValues();
    void onItemChanged(QTableWidgetItem *item);
    qint64 currentVariableId() const;

    Project *m_project;
    QTableWidget *m_table;
    bool m_refreshing = false;
};
