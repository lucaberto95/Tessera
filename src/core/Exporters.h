#pragma once

#include "core/Model.h"
#include "core/Xlsx.h"

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

// isoDates: dates as "yyyy-MM-dd HH:mm:ss" (local time), which the .xlsx export turns into real dates,
// instead of the short format of the user's locale.
Table segmentsTable(const Project &project, const QList<Segment> &segments, bool isoDates = false);
bool exportSegmentsHtml(const Project &project, const QList<Segment> &segments, const QString &path,
                        QString *error);

Table codebookTable(const Project &project);
bool exportCodebookHtml(const Project &project, const QString &path, QString *error);

Table variablesTable(const Project &project);
Table documentsTable(const Project &project, bool isoDates = false);
Table memosTable(const Project &project, bool isoDates = false);

// "Codice: Tema > Sotto", "Documento: Intervista"… for a memo target.
QString memoTargetLabel(const Project &project, MemoTarget type, qint64 targetId);
// Every element a memo is attached to, or "Progetto" / "Memo libero" when it has none.
QString memoLinksLabel(const Project &project, const Memo &memo);

// Sheets for the .xlsx export (Xlsx::writeWorkbook): names and free text kept as text, the code's
// colour behind the code's cell, long text wrapped, numbers and dates typed.
Xlsx::Sheet segmentsSheet(const Project &project, const QList<Segment> &segments);
Xlsx::Sheet codebookSheet(const Project &project);
Xlsx::Sheet variablesSheet(const Project &project);
Xlsx::Sheet documentsSheet(const Project &project);
Xlsx::Sheet memosSheet(const Project &project);
// A table computed elsewhere (matrices, statistics): the first column holds the row labels.
Xlsx::Sheet tableSheet(const QString &name, const Table &rows);
// "Export the whole project": documents, codes, segments, variables and memos, one sheet each.
QList<Xlsx::Sheet> projectSheets(const Project &project);
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
