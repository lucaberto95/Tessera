#pragma once

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <utility>

// Minimal reader and writer for Excel .xlsx workbooks (a ZIP archive of XML parts), without external
// libraries.
namespace Xlsx {

using Table = QList<QStringList>;

// Reads the cell values of the first worksheet, one QStringList per row (row 1 first). Cells and rows
// missing from the file are empty strings; trailing empty rows are dropped. Values are returned as text:
// shared and inline strings as they are, numbers in the C locale ("3.5"), cells formatted as dates as
// ISO "yyyy-MM-dd" (plus " HH:mm:ss" when they have a time), times as "HH:mm:ss", booleans as
// "VERO"/"FALSO" and error cells (#N/D...) as empty strings.
Table readFirstSheet(const QString &path, QString *error);
// The same for the worksheet at `index` in the workbook's order.
Table readSheet(const QString &path, int index, QString *error);
// Names of the worksheets, in the workbook's order (empty when the file is not a workbook).
QStringList sheetNames(const QString &path);

// Excel serial date (days since 1899-12-30, or since 1904-01-01 in the 1904 date system) to a date and
// time. Serials before 1900-03-01 account for Excel's nonexistent 1900-02-29.
QDateTime serialToDateTime(double serial, bool date1904 = false);

// Excel's limit on the characters of a cell.
inline constexpr int MaxCellLength = 32767;

// One worksheet for writeWorkbook. Row 0 is the header: bold, frozen and with an autofilter. The other
// cells that look like numbers in the C locale ("12", "-3.5") or like ISO dates ("2024-05-01",
// "2024-05-01 10:30[:00]") become numbers and dates, except in the text columns.
struct Sheet {
    QString name;
    Table rows;
    QSet<int> textColumns;                   // names and free text: always written as text
    QSet<int> wrapColumns;                   // long text: wrapped, in a wider column
    QMap<std::pair<int, int>, QColor> fills; // (row, column) -> background; translucent colours are laid on white
};

// Writes a workbook with one worksheet per sheet, strings inline (no shared strings table). Sheet
// names are made acceptable to Excel (at most 31 characters, none of []:*?/\, unique). Characters XML
// does not allow are dropped and longer cells are cut to MaxCellLength: *truncatedCells (optional)
// counts them, so that the caller can warn the user.
bool writeWorkbook(const QString &path, const QList<Sheet> &sheets, QString *error, int *truncatedCells = nullptr);

} // namespace Xlsx
