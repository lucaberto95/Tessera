#pragma once

#include "core/Model.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class Project;

namespace Exporters {

using Table = QList<QStringList>;

// CSV for Excel: UTF-8 with BOM, ';' separator (matches Italian/European Excel defaults).
bool writeCsv(const QString &path, const Table &rows, QString *error);
Table readCsv(const QString &path, QString *error);

bool writeHtmlTable(const QString &path, const QString &title, const Table &rows, QString *error);

Table segmentsTable(const Project &project, const QList<Segment> &segments);
bool exportSegmentsHtml(const Project &project, const QList<Segment> &segments, const QString &path,
                        QString *error);

Table codebookTable(const Project &project);
bool exportCodebookHtml(const Project &project, const QString &path, QString *error);

Table variablesTable(const Project &project);
// Validates a user/CSV value against a variable type and returns it in the stored form:
// decimals with '.', dates as ISO yyyy-MM-dd, booleans as "1"/"0". Empty input is valid.
bool normalizeVariableValue(VariableType type, const QString &input, QString *out);
// Picks the narrowest type that every non-empty value respects (Text when none fits).
VariableType inferVariableType(const QStringList &values);

struct CsvVariableColumn {
    QString name;
    VariableType type = VariableType::Text; // existing variable's type, or the inferred one
    bool existing = false;
    int invalidCount = 0; // existing variables only: values that do not fit the type
    QStringList samples;
};
struct CsvVariablesPreview {
    QList<CsvVariableColumn> columns;
    int matchedRows = 0;
    QStringList unmatchedDocuments;
    Table sampleRows; // header + first data rows
};

// Matches rows to documents by name (first column, or a column called "Documento"/"Document").
bool previewVariablesCsv(const Project &project, const QString &path, CsvVariablesPreview *preview, QString *error);
// `types` overrides the type of new variables by column name; the default is the inferred one.
bool importVariablesCsv(Project &project, const QString &path, QString *report, QString *error,
                        const QHash<QString, VariableType> &types = {});

QString htmlEscape(const QString &s);

} // namespace Exporters
