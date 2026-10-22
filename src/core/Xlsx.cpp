#include "core/Xlsx.h"

#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QXmlStreamReader>
#include <QtCore/private/qzipreader_p.h>

#include <cmath>

namespace Xlsx {

namespace {

// Excel's own limits: anything beyond comes from a damaged or hostile file.
constexpr int MaxRows = 1048576;
constexpr int MaxColumns = 16384;
constexpr int MaxTextDepth = 32;

enum class NumberKind { Number, Date, Time };

// Text of a shared string or inline string: the <t> elements, also inside rich text runs (<r>),
// without the phonetic guides (<rPh>).
void collectText(QXmlStreamReader &r, QString *out, int depth = 0)
{
    while (r.readNextStartElement()) {
        if (r.name() == QLatin1String("t"))
            *out += r.readElementText();
        else if (r.name() == QLatin1String("rPh") || depth >= MaxTextDepth)
            r.skipCurrentElement();
        else
            collectText(r, out, depth + 1);
    }
}

// Whether a number format code shows a date, a time or a plain number. Quoted literals, escaped
// characters and [..] sections (colours, conditions, locales) are not part of the pattern.
NumberKind classifyFormat(const QString &code)
{
    QString s;
    for (qsizetype i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('"')) {
            const qsizetype close = code.indexOf(QLatin1Char('"'), i + 1);
            i = close < 0 ? code.size() : close;
        } else if (c == QLatin1Char('\\') || c == QLatin1Char('_') || c == QLatin1Char('*')) {
            ++i; // the next character is a literal (or padding)
        } else if (c == QLatin1Char('[')) {
            const qsizetype close = code.indexOf(QLatin1Char(']'), i + 1);
            i = close < 0 ? code.size() : close;
        } else if (c == QLatin1Char(';')) {
            break; // only the first section (positive numbers)
        } else {
            s += c.toLower();
        }
    }
    const bool hasTime = s.contains(QLatin1Char('h')) || s.contains(QLatin1Char('s'));
    const bool hasDate = s.contains(QLatin1Char('d')) || s.contains(QLatin1Char('y'))
        || (s.contains(QLatin1Char('m')) && !hasTime); // "m" next to h/s means minutes
    if (hasDate)
        return NumberKind::Date;
    return hasTime ? NumberKind::Time : NumberKind::Number;
}

NumberKind builtInFormatKind(int id)
{
    if ((id >= 14 && id <= 17) || id == 22 || (id >= 27 && id <= 36) || (id >= 50 && id <= 58))
        return NumberKind::Date;
    if ((id >= 18 && id <= 21) || (id >= 45 && id <= 47))
        return NumberKind::Time;
    return NumberKind::Number;
}

// Number kind of each cell style (index = the cell's "s" attribute).
QList<NumberKind> readStyles(const QByteArray &xml)
{
    QHash<int, NumberKind> customFormats;
    QList<NumberKind> styles;
    QXmlStreamReader r(xml);
    bool inCellXfs = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            if (r.name() == QLatin1String("numFmt")) {
                const int id = r.attributes().value(QLatin1String("numFmtId")).toInt();
                customFormats.insert(id, classifyFormat(r.attributes().value(QLatin1String("formatCode")).toString()));
            } else if (r.name() == QLatin1String("cellXfs")) {
                inCellXfs = true;
            } else if (inCellXfs && r.name() == QLatin1String("xf")) {
                const int id = r.attributes().value(QLatin1String("numFmtId")).toInt();
                styles << customFormats.value(id, builtInFormatKind(id));
            }
        } else if (r.isEndElement() && r.name() == QLatin1String("cellXfs")) {
            inCellXfs = false;
        }
    }
    return styles;
}

QStringList readSharedStrings(const QByteArray &xml)
{
    QStringList strings;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("si")) {
            QString s;
            collectText(r, &s);
            strings << s;
        }
    }
    return strings;
}

// "BC12" -> column 54 (0-based), row 11 (0-based); -1 for a missing part.
void parseCellRef(QStringView ref, int *column, int *row)
{
    qint64 col = 0;
    qsizetype i = 0;
    for (; i < ref.size(); ++i) {
        const char16_t c = ref.at(i).toUpper().unicode();
        if (c < u'A' || c > u'Z')
            break;
        col = col * 26 + (c - u'A' + 1);
        if (col > MaxColumns)
            break;
    }
    *column = (col > 0 && col <= MaxColumns) ? int(col - 1) : -1;
    bool ok = false;
    const int r = ref.mid(i).toInt(&ok);
    *row = (ok && r > 0 && r <= MaxRows) ? r - 1 : -1;
}

QString formatSerial(double serial, NumberKind kind, bool date1904)
{
    const QDateTime dt = serialToDateTime(serial, date1904);
    if (!dt.isValid())
        return QString::number(serial, 'g', 15);
    if (kind == NumberKind::Time)
        return dt.time().toString(QStringLiteral("HH:mm:ss"));
    const QString date = dt.date().toString(Qt::ISODate);
    return dt.time() == QTime(0, 0) ? date : date + QLatin1Char(' ') + dt.time().toString(QStringLiteral("HH:mm:ss"));
}

struct Workbook {
    QString sheetPath = QStringLiteral("xl/worksheets/sheet1.xml");
    bool date1904 = false;
};

// Path of the first sheet listed in the workbook, through the workbook's relationships.
Workbook readWorkbook(QZipReader &zip)
{
    Workbook wb;
    QString firstSheetRel;
    QXmlStreamReader r(zip.fileData(QStringLiteral("xl/workbook.xml")));
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        if (r.name() == QLatin1String("workbookPr")) {
            const auto v = r.attributes().value(QLatin1String("date1904"));
            wb.date1904 = v == QLatin1String("1") || v == QLatin1String("true");
        } else if (r.name() == QLatin1String("sheet") && firstSheetRel.isEmpty()) {
            for (const QXmlStreamAttribute &a : r.attributes()) {
                if (a.name() == QLatin1String("id") && !a.prefix().isEmpty())
                    firstSheetRel = a.value().toString();
            }
        }
    }
    if (firstSheetRel.isEmpty())
        return wb;

    QXmlStreamReader rels(zip.fileData(QStringLiteral("xl/_rels/workbook.xml.rels")));
    while (!rels.atEnd()) {
        rels.readNext();
        if (rels.isStartElement() && rels.name() == QLatin1String("Relationship")
            && rels.attributes().value(QLatin1String("Id")) == firstSheetRel) {
            QString target = rels.attributes().value(QLatin1String("Target")).toString();
            if (target.startsWith(QLatin1Char('/')))
                target = target.mid(1); // absolute within the package
            else
                target.prepend(QStringLiteral("xl/"));
            if (!target.contains(QLatin1String("..")))
                wb.sheetPath = target;
            break;
        }
    }
    return wb;
}

} // namespace

QDateTime serialToDateTime(double serial, bool date1904)
{
    if (!std::isfinite(serial) || serial < 0 || serial >= 2958466) // 2958465 = 9999-12-31
        return {};
    qint64 days = qint64(std::floor(serial));
    qint64 seconds = std::llround((serial - double(days)) * 86400.0);
    if (seconds >= 86400) {
        ++days;
        seconds -= 86400;
    }
    QDate base;
    if (date1904)
        base = QDate(1904, 1, 1);
    else
        base = days < 60 ? QDate(1899, 12, 31) : QDate(1899, 12, 30); // Excel counts 1900-02-29
    return QDateTime(base.addDays(days), QTime(0, 0).addSecs(int(seconds)));
}

Table readFirstSheet(const QString &path, QString *error)
{
    const QString fileName = QFileInfo(path).fileName();
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        *error = QObject::tr("%1 non è una cartella di lavoro Excel valida").arg(fileName);
        return {};
    }
    const Workbook wb = readWorkbook(zip);
    const QByteArray sheetXml = zip.fileData(wb.sheetPath);
    if (sheetXml.isEmpty()) {
        *error = QObject::tr("%1 non contiene fogli di lavoro leggibili").arg(fileName);
        return {};
    }
    const QStringList shared = readSharedStrings(zip.fileData(QStringLiteral("xl/sharedStrings.xml")));
    const QList<NumberKind> styles = readStyles(zip.fileData(QStringLiteral("xl/styles.xml")));

    Table table;
    QXmlStreamReader r(sheetXml);
    int rowIndex = -1;
    int columnIndex = -1;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        if (r.name() == QLatin1String("row")) {
            int col = 0, row = -1;
            const auto ref = r.attributes().value(QLatin1String("r"));
            if (!ref.isEmpty())
                parseCellRef(QStringLiteral("A") + ref.toString(), &col, &row);
            rowIndex = row >= 0 ? row : rowIndex + 1;
            columnIndex = -1;
            continue;
        }
        if (r.name() != QLatin1String("c"))
            continue;

        const QXmlStreamAttributes attrs = r.attributes();
        int col = -1, row = -1;
        if (attrs.hasAttribute(QLatin1String("r")))
            parseCellRef(attrs.value(QLatin1String("r")), &col, &row);
        if (row >= 0)
            rowIndex = row;
        rowIndex = qMax(rowIndex, 0);
        columnIndex = col >= 0 ? col : columnIndex + 1;
        const QString type = attrs.value(QLatin1String("t")).toString();
        const int style = attrs.value(QLatin1String("s")).toInt();

        QString raw;
        QString inlineText;
        bool hasInline = false;
        while (r.readNextStartElement()) {
            if (r.name() == QLatin1String("v")) {
                raw = r.readElementText();
            } else if (r.name() == QLatin1String("is")) {
                hasInline = true;
                collectText(r, &inlineText);
            } else {
                r.skipCurrentElement(); // formula, extensions
            }
        }

        QString value;
        if (type == QLatin1String("s")) {
            bool ok = false;
            const int index = raw.toInt(&ok);
            if (ok && index >= 0 && index < shared.size())
                value = shared.at(index);
        } else if (type == QLatin1String("inlineStr") || hasInline) {
            value = inlineText;
        } else if (type == QLatin1String("str")) {
            value = raw;
        } else if (type == QLatin1String("b")) {
            if (!raw.isEmpty())
                value = raw.trimmed() == QLatin1String("1") ? QObject::tr("VERO") : QObject::tr("FALSO");
        } else if (type == QLatin1String("e")) {
            value.clear();
        } else if (type == QLatin1String("d")) {
            const QDateTime dt = QDateTime::fromString(raw.trimmed(), Qt::ISODate);
            value = !dt.isValid() ? raw.trimmed()
                : dt.time() == QTime(0, 0) ? dt.date().toString(Qt::ISODate)
                                           : dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        } else if (!raw.isEmpty()) {
            bool ok = false;
            const double number = raw.trimmed().toDouble(&ok);
            const NumberKind kind = (style >= 0 && style < styles.size()) ? styles.at(style) : NumberKind::Number;
            if (!ok)
                value = raw;
            else if (kind == NumberKind::Number)
                value = QString::number(number, 'g', 15);
            else
                value = formatSerial(number, kind, wb.date1904);
        }

        if (columnIndex < 0 || columnIndex >= MaxColumns || rowIndex >= MaxRows || value.isEmpty())
            continue;
        while (table.size() <= rowIndex)
            table.append(QStringList());
        QStringList &cells = table[rowIndex];
        while (cells.size() <= columnIndex)
            cells.append(QString());
        cells[columnIndex] = value;
    }
    if (r.hasError()) {
        *error = QObject::tr("Errore nel leggere %1: %2").arg(fileName, r.errorString());
        return {};
    }
    return table;
}

} // namespace Xlsx
