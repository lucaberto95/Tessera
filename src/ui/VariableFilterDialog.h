#pragma once

#include "core/VariableFilter.h"

#include <QDialog>

class Project;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// Builds a list of conditions on document variables and activates the documents that satisfy them.
class VariableFilterDialog : public QDialog {
    Q_OBJECT
public:
    explicit VariableFilterDialog(Project *project, QWidget *parent = nullptr);

    QList<VariableFilter::Condition> conditions() const;
    bool matchAny() const;
    void setConditions(const QList<VariableFilter::Condition> &conditions, bool any);
    QList<qint64> matchingDocuments() const;

signals:
    void activationRequested(const QList<qint64> &documentIds);
    void setRequested(const QList<qint64> &documentIds);

private:
    struct Row {
        QWidget *widget = nullptr;
        QComboBox *variable = nullptr;
        QComboBox *op = nullptr;
        QLineEdit *value = nullptr;
        QLineEdit *value2 = nullptr;
    };

    void addRow(const VariableFilter::Condition &condition);
    void removeRow(QWidget *widget);
    void fillOperators(Row &row, VariableFilter::Op selected);
    void updateRowState(const Row &row);
    void updateState();
    void remember();
    VariableType typeOf(qint64 variableId) const;

    Project *m_project;
    QComboBox *m_combine;
    QVBoxLayout *m_rowsLayout;
    QList<Row> m_rows;
    QLabel *m_preview;
    QPushButton *m_activate;
    QPushButton *m_saveSet;
};
