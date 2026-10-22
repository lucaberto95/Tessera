#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Minimal reader for Excel .xlsx workbooks (a ZIP archive of XML parts), without external libraries.
namespace Xlsx {

using Table = QList<QStringList>;

// Reads the cell values of the first worksheet, one QStringList per row (row 1 first). Cells and rows
// missing from the file are empty strings; trailing empty rows are dropped. Values are returned as text:
// shared and inline strings as they are, numbers in the C locale ("3.5"), cells formatted as dates as
// ISO "yyyy-MM-dd" (plus " HH:mm:ss" when they have a time), times as "HH:mm:ss", booleans as
// "VERO"/"FALSO" and error cells (#N/D...) as empty strings.
Table readFirstSheet(const QString &path, QString *error);

// Excel serial date (days since 1899-12-30, or since 1904-01-01 in the 1904 date system) to a date and
// time. Serials before 1900-03-01 account for Excel's nonexistent 1900-02-29.
QDateTime serialToDateTime(double serial, bool date1904 = false);

} // namespace Xlsx
