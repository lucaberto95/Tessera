#pragma once

#include "core/Model.h"

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
// Matches rows to documents by name (first column, or a column called "Documento"/"Document").
bool importVariablesCsv(Project &project, const QString &path, QString *report, QString *error);

QString htmlEscape(const QString &s);

} // namespace Exporters
