#pragma once

#include "core/Model.h"

#include <QList>
#include <QString>
#include <QStringList>

class Project;

// Import of survey data (one row per respondent): open answers become the text of one document per
// respondent, coded with a code named after the question; closed answers become document variables.
namespace Survey {

using Table = QList<QStringList>;

enum class Role { Ignore, Id, Variable, OpenAnswer };

struct Column {
    QString name; // the header, on one line ("Colonna N" when empty)
    Role role = Role::Ignore;
    VariableType type = VariableType::Text; // for Role::Variable
    bool existingVariable = false;          // a variable with this name exists: its type is kept
    QStringList samples;
};

// Reads an .xlsx workbook (first sheet) or a CSV file. Leading empty rows are dropped, so the first
// row of the result is the header.
Table readTable(const QString &path, QString *error);

// One column per header cell, with a proposed role: the respondent ID from its name, existing
// variables and non-text values as variables, long or wordy free text as open answers.
QList<Column> proposeColumns(const Project &project, const Table &table);

struct Options {
    QList<Column> columns;
    QString groupName;       // the new document group
    bool codeAnswers = true; // code each answer with a top-level code named after the question
    QString sourcePath;
};

struct Report {
    qint64 groupId = 0;
    qint64 firstDocumentId = 0;
    int documents = 0;
    int codedAnswers = 0;
    int newCodes = 0;
    int newVariables = 0;
    int skippedRows = 0; // respondents without any open answer
    QStringList invalidValues;
    QString text() const;
};

// Text of a respondent's document: each non-empty answer is a paragraph preceded by its question,
// with a blank line between answers. `answerRanges` receives, per answer, the column and its range.
struct AnswerRange {
    int column = 0;
    int start = 0;
    int end = 0;
};
QString respondentText(const QList<Column> &columns, const QStringList &row, QList<AnswerRange> *answerRanges);

// Creates the group, documents, codes, codings and variable values in one undo step.
bool importSurvey(Project &project, const Table &table, const Options &options, Report *report, QString *error);

} // namespace Survey
