#pragma once

#include "core/Survey.h"

#include <QDialog>

class Project;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Preview of a survey table: the role of each column (respondent ID, variable with its type, open
// answer, ignored), the name of the new document group and whether answers are coded by question.
class SurveyImportDialog : public QDialog {
    Q_OBJECT
public:
    SurveyImportDialog(Project *project, const Survey::Table &table, const QString &fileName,
                       QWidget *parent = nullptr);

    Survey::Options options() const;
    void setColumnRole(int column, Survey::Role role);
    void setColumnType(int column, VariableType type);
    int respondentCount() const;

private:
    void onRoleChanged(int column);
    void updateState();

    Survey::Table m_table;
    QList<Survey::Column> m_columns;
    QTableWidget *m_columnsTable;
    QList<QComboBox *> m_roles;
    QList<QComboBox *> m_types;
    QLineEdit *m_groupName;
    QCheckBox *m_codeAnswers;
    QLabel *m_summary;
    QPushButton *m_import;
};
