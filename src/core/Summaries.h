#pragma once

#include "core/Exporters.h"
#include "core/Model.h"

#include <QList>
#include <QString>

class Project;

// Logic of the summary grid and the summary tables (issue #14). Pure functions over a Project.
namespace Summaries {

// What a saved summary table shows. Stored in summary_tables.config_json as
// {"documents":[ids], "set":id, "codes":[ids], "variables":[ids]}. Rows are the documents of the set
// when `setId` is non-zero (read again at every generation, so a set that changes is followed), else
// `documentIds`. Ids of elements that no longer exist are skipped.
struct TableConfig {
    QList<qint64> documentIds;
    qint64 setId = 0;
    QList<qint64> codeIds;
    QList<qint64> variableIds;
};

QString configToJson(const TableConfig &config);
TableConfig configFromJson(const QString &json);

// Documents of the table in document tree order.
QList<qint64> tableDocuments(const Project &project, const TableConfig &config);

// Header: "Documento", the variables, then the code paths. One row per document; a cell holds the summary
// of that document and code (empty if none).
Exporters::Table buildTable(const Project &project, const TableConfig &config);
// The same table as a worksheet: names and summaries are text, summaries wrapped.
Xlsx::Sheet buildSheet(const Project &project, const TableConfig &config, const QString &sheetName);

// Text of a segment as shown in the grid: the coded text, the text or description of an area, or the
// time range.
QString segmentText(const Project &project, const Segment &segment);

} // namespace Summaries
