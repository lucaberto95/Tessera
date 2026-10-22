#include "core/Xlsx.h"

#include <QBuffer>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace Xlsx {

namespace {

// Excel's own limits: anything beyond comes from a damaged or hostile file.
constexpr int MaxRows = 1048576;
constexpr int MaxColumns = 16384;
constexpr int MaxTextDepth = 32;

enum class NumberKind { Number, Date, Time };

// Excel writes a character as "_xHHHH_" (e.g. a control character) and a literal "_xHHHH_" with
// its underscore escaped as "_x005F_".
QString decodeEscapes(const QString &s)
{
    if (!s.contains(QLatin1String("_x")))
        return s;
    static const QRegularExpression escape(QStringLiteral("_x([0-9A-Fa-f]{4})_"));
    QString out;
    qsizetype last = 0;
    for (auto it = escape.globalMatch(s); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(s).mid(last, m.capturedStart() - last);
        out += QChar(char16_t(m.capturedView(1).toUInt(nullptr, 16)));
        last = m.capturedEnd();
    }
    out += QStringView(s).mid(last);
    return out;
}

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
            strings << decodeEscapes(s);
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
    QStringList names;
    QStringList sheetPaths; // empty when the relationship is missing or points outside the package
    bool date1904 = false;
};

// Sheets listed in the workbook, with their paths through the workbook's relationships.
Workbook readWorkbook(QZipReader &zip)
{
    Workbook wb;
    QStringList sheetRels;
    QXmlStreamReader r(zip.fileData(QStringLiteral("xl/workbook.xml")));
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        if (r.name() == QLatin1String("workbookPr")) {
            const auto v = r.attributes().value(QLatin1String("date1904"));
            wb.date1904 = v == QLatin1String("1") || v == QLatin1String("true");
        } else if (r.name() == QLatin1String("sheet")) {
            QString rel;
            for (const QXmlStreamAttribute &a : r.attributes()) {
                if (a.name() == QLatin1String("id") && !a.prefix().isEmpty())
                    rel = a.value().toString();
            }
            wb.names << r.attributes().value(QLatin1String("name")).toString();
            sheetRels << rel;
        }
    }

    QHash<QString, QString> targets;
    QXmlStreamReader rels(zip.fileData(QStringLiteral("xl/_rels/workbook.xml.rels")));
    while (!rels.atEnd()) {
        rels.readNext();
        if (rels.isStartElement() && rels.name() == QLatin1String("Relationship")) {
            QString target = rels.attributes().value(QLatin1String("Target")).toString();
            if (target.startsWith(QLatin1Char('/')))
                target = target.mid(1); // absolute within the package
            else
                target.prepend(QStringLiteral("xl/"));
            if (!target.contains(QLatin1String("..")))
                targets.insert(rels.attributes().value(QLatin1String("Id")).toString(), target);
        }
    }
    for (const QString &rel : std::as_const(sheetRels))
        wb.sheetPaths << targets.value(rel);
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

QStringList sheetNames(const QString &path)
{
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return {};
    return readWorkbook(zip).names;
}

Table readFirstSheet(const QString &path, QString *error)
{
    return readSheet(path, 0, error);
}

Table readSheet(const QString &path, int index, QString *error)
{
    const QString fileName = QFileInfo(path).fileName();
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        *error = QObject::tr("%1 non è una cartella di lavoro Excel valida").arg(fileName);
        return {};
    }
    const Workbook wb = readWorkbook(zip);
    QString sheetPath = index >= 0 && index < wb.sheetPaths.size() ? wb.sheetPaths.at(index) : QString();
    if (sheetPath.isEmpty() && index == 0)
        sheetPath = QStringLiteral("xl/worksheets/sheet1.xml"); // the usual name
    const QByteArray sheetXml = sheetPath.isEmpty() ? QByteArray() : zip.fileData(sheetPath);
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
            value = decodeEscapes(inlineText);
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

// ---------------------------------------------------------------- writer

namespace {

const char *const kXmlHeader = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
const char *const kMainNs = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const char *const kDocRelNs = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

constexpr int DateFormat = 14;     // built-in short date, shown in the user's locale
constexpr int DateTimeFormat = 22; // built-in date and time
constexpr int WidthSampleRows = 2000;

enum class CellKind { Text, Number, Date, DateTime };

// Translucent colours (the heatmaps on screen) laid on white: cell fills have no alpha.
QRgb opaque(const QColor &c)
{
    const double a = c.alphaF();
    const auto mix = [a](int v) { return int(std::lround(255 + (v - 255) * a)); };
    return qRgb(mix(c.red()), mix(c.green()), mix(c.blue()));
}

bool isDark(QRgb rgb)
{
    return qRed(rgb) * 299 + qGreen(rgb) * 587 + qBlue(rgb) * 114 < 140000;
}

QString argb(QRgb rgb)
{
    return QStringLiteral("FF%1").arg(rgb & 0xFFFFFF, 6, 16, QLatin1Char('0')).toUpper();
}

// The cell formats (cellXfs) the sheets use, created on demand. Fonts: 0 normal, 1 bold, 2 and 3 the
// same in white for dark fills. Fills 0 and 1 are the two Excel reserves.
class Styles {
public:
    int index(bool bold, int numFmt, bool wrap, const QColor &fill)
    {
        int fillId = 0;
        bool white = false;
        if (fill.isValid()) {
            const QRgb rgb = opaque(fill);
            fillId = m_fillIds.value(rgb);
            if (!fillId) {
                fillId = int(m_fills.size()) + 2;
                m_fills << rgb;
                m_fillIds.insert(rgb, fillId);
            }
            white = isDark(rgb);
        }
        const Xf key{(bold ? 1 : 0) + (white ? 2 : 0), fillId, numFmt, wrap};
        const auto it = m_index.constFind(key);
        if (it != m_index.cend())
            return *it;
        const int id = int(m_xfs.size());
        m_xfs << key;
        m_index.insert(key, id);
        return id;
    }

    QByteArray xml() const
    {
        QString s = QString::fromLatin1(kXmlHeader) + QStringLiteral("<styleSheet xmlns=\"%1\">").arg(QLatin1String(kMainNs));
        s += QStringLiteral("<fonts count=\"4\">");
        for (int f = 0; f < 4; ++f)
            s += QStringLiteral("<font>%1<sz val=\"11\"/>%2<name val=\"Calibri\"/><family val=\"2\"/></font>")
                     .arg(f & 1 ? QStringLiteral("<b/>") : QString(),
                          f & 2 ? QStringLiteral("<color rgb=\"FFFFFFFF\"/>") : QString());
        s += QStringLiteral("</fonts><fills count=\"%1\"><fill><patternFill patternType=\"none\"/></fill>"
                            "<fill><patternFill patternType=\"gray125\"/></fill>")
                 .arg(m_fills.size() + 2);
        for (QRgb rgb : m_fills)
            s += QStringLiteral("<fill><patternFill patternType=\"solid\"><fgColor rgb=\"%1\"/>"
                                "<bgColor indexed=\"64\"/></patternFill></fill>")
                     .arg(argb(rgb));
        s += QStringLiteral("</fills><borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border>"
                            "</borders><cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" "
                            "borderId=\"0\"/></cellStyleXfs><cellXfs count=\"%1\">")
                 .arg(m_xfs.size());
        for (const Xf &xf : m_xfs) {
            const auto [font, fill, numFmt, wrap] = xf;
            s += QStringLiteral("<xf numFmtId=\"%1\" fontId=\"%2\" fillId=\"%3\" borderId=\"0\" xfId=\"0\"")
                     .arg(numFmt).arg(font).arg(fill);
            if (numFmt)
                s += QStringLiteral(" applyNumberFormat=\"1\"");
            if (font)
                s += QStringLiteral(" applyFont=\"1\"");
            if (fill)
                s += QStringLiteral(" applyFill=\"1\"");
            s += wrap ? QStringLiteral(" applyAlignment=\"1\"><alignment vertical=\"top\" wrapText=\"1\"/></xf>")
                      : QStringLiteral("/>");
        }
        s += QStringLiteral("</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/>"
                            "</cellStyles></styleSheet>");
        return s.toUtf8();
    }

private:
    using Xf = std::tuple<int, int, int, bool>; // font, fill, number format, wrap
    QList<Xf> m_xfs{Xf{0, 0, 0, false}};
    QMap<Xf, int> m_index{{Xf{0, 0, 0, false}, 0}};
    QList<QRgb> m_fills;
    QHash<QRgb, int> m_fillIds;
};

QString columnName(int column)
{
    QString s;
    for (int n = column + 1; n > 0; n = (n - 1) / 26)
        s.prepend(QChar(char16_t(u'A' + (n - 1) % 26)));
    return s;
}

QString cellRef(int row, int column)
{
    return columnName(column) + QString::number(row + 1);
}

// Text as a cell can hold it: without the characters XML 1.0 forbids (control characters, lone
// surrogates), with a literal "_xHHHH_" escaped (Excel would read it as character HHHH), cut to
// MaxCellLength.
QString cellText(const QString &in, bool *truncated)
{
    QString s;
    s.reserve(in.size());
    for (qsizetype i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        const char16_t u = c.unicode();
        if (c.isHighSurrogate()) {
            if (i + 1 < in.size() && in.at(i + 1).isLowSurrogate()) {
                s += c;
                s += in.at(++i);
            }
        } else if (!c.isLowSurrogate() && (u >= 0x20 || u == u'\t' || u == u'\n') && u != 0xFFFE && u != 0xFFFF) {
            s += c;
        }
    }
    if (s.contains(QLatin1String("_x"))) {
        static const QRegularExpression escape(QStringLiteral("_(x[0-9A-Fa-f]{4}_)"));
        s.replace(escape, QStringLiteral("_x005F_\\1"));
    }
    if (s.size() > MaxCellLength) {
        qsizetype n = MaxCellLength;
        if (s.at(n - 1).isHighSurrogate())
            --n;
        s.truncate(n);
        *truncated = true;
    }
    return s;
}

// Numbers in the C locale (no leading zeros: "007" is a code, not a number) and ISO dates.
CellKind classify(const QString &s, double *serial)
{
    static const QRegularExpression number(QStringLiteral("^-?(0|[1-9]\\d{0,14})(\\.\\d{1,15})?$"));
    static const QRegularExpression date(
        QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})(?:[T ](\\d{2}):(\\d{2})(?::(\\d{2}))?)?$"));
    if (s.isEmpty())
        return CellKind::Text;
    if (number.match(s).hasMatch())
        return CellKind::Number;
    const QRegularExpressionMatch m = date.match(s);
    if (!m.hasMatch())
        return CellKind::Text;
    const QDate d(m.capturedView(1).toInt(), m.capturedView(2).toInt(), m.capturedView(3).toInt());
    if (!d.isValid() || d < QDate(1900, 3, 1)) // earlier serials count Excel's nonexistent 1900-02-29
        return CellKind::Text;
    const bool hasTime = m.hasCaptured(4);
    const QTime t = hasTime ? QTime(m.capturedView(4).toInt(), m.capturedView(5).toInt(), m.capturedView(6).toInt())
                            : QTime(0, 0);
    if (!t.isValid())
        return CellKind::Text;
    *serial = double(QDate(1899, 12, 30).daysTo(d)) + t.msecsSinceStartOfDay() / 86400000.0;
    return hasTime ? CellKind::DateTime : CellKind::Date;
}

// Excel refuses sheet names longer than 31 characters, with []:*?/\, starting or ending with an
// apostrophe, equal to another sheet's (ignoring case) or to the reserved "History".
QStringList validSheetNames(const QList<Sheet> &sheets)
{
    QStringList names;
    QSet<QString> used;
    for (qsizetype i = 0; i < sheets.size(); ++i) {
        QString name;
        for (const QChar c : sheets[i].name) {
            if (QStringLiteral("[]:*?/\\").contains(c))
                name += QLatin1Char('_');
            else if (c.unicode() >= 0x20)
                name += c;
        }
        name = name.trimmed();
        while (name.startsWith(QLatin1Char('\'')))
            name.remove(0, 1);
        while (name.endsWith(QLatin1Char('\'')))
            name.chop(1);
        if (name.isEmpty())
            name = QObject::tr("Foglio %1").arg(i + 1);
        if (name.compare(QLatin1String("History"), Qt::CaseInsensitive) == 0)
            name += QLatin1Char('_');
        const auto fit = [](QString s, qsizetype room) {
            if (s.size() > room) {
                s.truncate(room);
                if (s.back().isHighSurrogate())
                    s.chop(1);
            }
            return s;
        };
        QString unique = fit(name, 31);
        for (int n = 2; used.contains(unique.toLower()); ++n) {
            const QString suffix = QStringLiteral(" (%1)").arg(n);
            unique = fit(name, 31 - suffix.size()) + suffix;
        }
        used.insert(unique.toLower());
        names << unique;
    }
    return names;
}

int longestLine(const QString &s)
{
    int longest = 0;
    int current = 0;
    for (const QChar c : s) {
        if (c == QLatin1Char('\n')) {
            current = 0;
        } else {
            ++current;
            longest = std::max(longest, current);
        }
    }
    return longest;
}

// Column widths in characters: fitted to the content, wider and wrapped for long text.
QList<int> columnWidths(const Sheet &sheet, int columns)
{
    QList<int> longest(columns, 0);
    for (qsizetype r = 0; r < sheet.rows.size() && r < WidthSampleRows; ++r) {
        const QStringList &row = sheet.rows[r];
        for (int c = 0; c < columns && c < row.size(); ++c)
            longest[c] = std::max(longest[c], longestLine(row[c]) + (r == 0 ? 3 : 0)); // room for the filter button
    }
    QList<int> widths;
    for (int c = 0; c < columns; ++c)
        widths << (sheet.wrapColumns.contains(c) ? std::clamp(longest[c] + 2, 12, 60) : std::clamp(longest[c] + 2, 6, 50));
    return widths;
}

QByteArray sheetXml(const Sheet &sheet, bool selected, Styles &styles, int *truncated, QString *filterRange)
{
    int columns = 0;
    for (const QStringList &row : sheet.rows)
        columns = std::max(columns, int(row.size()));
    const int rows = int(sheet.rows.size());
    const bool hasTable = rows > 0 && columns > 0;

    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("worksheet"));
    w.writeDefaultNamespace(QString::fromLatin1(kMainNs));
    w.writeEmptyElement(QStringLiteral("dimension"));
    w.writeAttribute(QStringLiteral("ref"), hasTable ? QStringLiteral("A1:") + cellRef(rows - 1, columns - 1)
                                                     : QStringLiteral("A1"));

    w.writeStartElement(QStringLiteral("sheetViews"));
    w.writeStartElement(QStringLiteral("sheetView"));
    if (selected)
        w.writeAttribute(QStringLiteral("tabSelected"), QStringLiteral("1"));
    w.writeAttribute(QStringLiteral("workbookViewId"), QStringLiteral("0"));
    if (rows > 1 && columns > 0) { // the header stays in view
        w.writeEmptyElement(QStringLiteral("pane"));
        w.writeAttribute(QStringLiteral("ySplit"), QStringLiteral("1"));
        w.writeAttribute(QStringLiteral("topLeftCell"), QStringLiteral("A2"));
        w.writeAttribute(QStringLiteral("activePane"), QStringLiteral("bottomLeft"));
        w.writeAttribute(QStringLiteral("state"), QStringLiteral("frozen"));
        w.writeEmptyElement(QStringLiteral("selection"));
        w.writeAttribute(QStringLiteral("pane"), QStringLiteral("bottomLeft"));
        w.writeAttribute(QStringLiteral("activeCell"), QStringLiteral("A2"));
        w.writeAttribute(QStringLiteral("sqref"), QStringLiteral("A2"));
    }
    w.writeEndElement(); // sheetView
    w.writeEndElement(); // sheetViews

    if (columns > 0) {
        w.writeStartElement(QStringLiteral("cols"));
        const QList<int> widths = columnWidths(sheet, columns);
        for (int c = 0; c < columns; ++c) {
            w.writeEmptyElement(QStringLiteral("col"));
            w.writeAttribute(QStringLiteral("min"), QString::number(c + 1));
            w.writeAttribute(QStringLiteral("max"), QString::number(c + 1));
            w.writeAttribute(QStringLiteral("width"), QString::number(widths[c]));
            w.writeAttribute(QStringLiteral("customWidth"), QStringLiteral("1"));
        }
        w.writeEndElement();
    }

    static const QColor headerFill(0xf2, 0xf2, 0xf2);
    w.writeStartElement(QStringLiteral("sheetData"));
    for (int r = 0; r < rows; ++r) {
        const QStringList &row = sheet.rows[r];
        bool rowOpen = false;
        for (int c = 0; c < columns; ++c) {
            const QString value = c < row.size() ? row[c] : QString();
            const auto fillIt = sheet.fills.constFind({r, c});
            const QColor fill = fillIt != sheet.fills.cend() ? *fillIt : r == 0 ? headerFill : QColor();
            if (value.isEmpty() && !fill.isValid())
                continue;
            double serial = 0;
            const CellKind kind =
                r == 0 || sheet.textColumns.contains(c) ? CellKind::Text : classify(value, &serial);
            const int numFmt = kind == CellKind::Date ? DateFormat : kind == CellKind::DateTime ? DateTimeFormat : 0;
            const int style = styles.index(r == 0, numFmt, r > 0 && sheet.wrapColumns.contains(c), fill);

            if (!rowOpen) {
                w.writeStartElement(QStringLiteral("row"));
                w.writeAttribute(QStringLiteral("r"), QString::number(r + 1));
                rowOpen = true;
            }
            w.writeStartElement(QStringLiteral("c"));
            w.writeAttribute(QStringLiteral("r"), cellRef(r, c));
            if (style)
                w.writeAttribute(QStringLiteral("s"), QString::number(style));
            if (kind == CellKind::Number) {
                w.writeTextElement(QStringLiteral("v"), value);
            } else if (kind != CellKind::Text) {
                w.writeTextElement(QStringLiteral("v"), QString::number(serial, 'g', 15));
            } else if (!value.isEmpty()) {
                bool cut = false;
                const QString text = cellText(value, &cut);
                *truncated += cut ? 1 : 0;
                w.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                w.writeStartElement(QStringLiteral("is"));
                w.writeStartElement(QStringLiteral("t"));
                if (!text.isEmpty() && (text.front().isSpace() || text.back().isSpace()))
                    w.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
                w.writeCharacters(text);
                w.writeEndElement(); // t
                w.writeEndElement(); // is
            }
            w.writeEndElement(); // c
        }
        if (rowOpen)
            w.writeEndElement(); // row
    }
    w.writeEndElement(); // sheetData

    if (hasTable) {
        const QString range = QStringLiteral("A1:") + cellRef(rows - 1, columns - 1);
        w.writeEmptyElement(QStringLiteral("autoFilter"));
        w.writeAttribute(QStringLiteral("ref"), range);
        *filterRange = QStringLiteral("$A$1:$%1$%2").arg(columnName(columns - 1)).arg(rows);
    }
    w.writeEmptyElement(QStringLiteral("pageMargins"));
    w.writeAttribute(QStringLiteral("left"), QStringLiteral("0.7"));
    w.writeAttribute(QStringLiteral("right"), QStringLiteral("0.7"));
    w.writeAttribute(QStringLiteral("top"), QStringLiteral("0.75"));
    w.writeAttribute(QStringLiteral("bottom"), QStringLiteral("0.75"));
    w.writeAttribute(QStringLiteral("header"), QStringLiteral("0.3"));
    w.writeAttribute(QStringLiteral("footer"), QStringLiteral("0.3"));
    w.writeEndElement(); // worksheet
    w.writeEndDocument();
    return out;
}

QByteArray workbookXml(const QStringList &names, const QStringList &filterRanges)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("workbook"));
    w.writeDefaultNamespace(QString::fromLatin1(kMainNs));
    w.writeNamespace(QString::fromLatin1(kDocRelNs), QStringLiteral("r"));
    w.writeStartElement(QStringLiteral("bookViews"));
    w.writeEmptyElement(QStringLiteral("workbookView"));
    w.writeEndElement();
    w.writeStartElement(QStringLiteral("sheets"));
    for (qsizetype i = 0; i < names.size(); ++i) {
        w.writeEmptyElement(QStringLiteral("sheet"));
        w.writeAttribute(QStringLiteral("name"), names[i]);
        w.writeAttribute(QStringLiteral("sheetId"), QString::number(i + 1));
        w.writeAttribute(QString::fromLatin1(kDocRelNs), QStringLiteral("id"), QStringLiteral("rId%1").arg(i + 1));
    }
    w.writeEndElement(); // sheets
    // Excel keeps the range of each autofilter in a hidden name.
    bool namesOpen = false;
    for (qsizetype i = 0; i < names.size(); ++i) {
        if (filterRanges[i].isEmpty())
            continue;
        if (!namesOpen) {
            w.writeStartElement(QStringLiteral("definedNames"));
            namesOpen = true;
        }
        w.writeStartElement(QStringLiteral("definedName"));
        w.writeAttribute(QStringLiteral("name"), QStringLiteral("_xlnm._FilterDatabase"));
        w.writeAttribute(QStringLiteral("localSheetId"), QString::number(i));
        w.writeAttribute(QStringLiteral("hidden"), QStringLiteral("1"));
        QString quoted = names[i];
        quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
        w.writeCharacters(QLatin1Char('\'') + quoted + QStringLiteral("'!") + filterRanges[i]);
        w.writeEndElement();
    }
    if (namesOpen)
        w.writeEndElement();
    w.writeEndElement(); // workbook
    w.writeEndDocument();
    return out;
}

} // namespace

bool writeWorkbook(const QString &path, const QList<Sheet> &sheets, QString *error, int *truncatedCells)
{
    if (truncatedCells)
        *truncatedCells = 0;
    if (sheets.isEmpty()) {
        *error = QObject::tr("Non c'è niente da esportare");
        return false;
    }
    const QStringList names = validSheetNames(sheets);
    for (qsizetype i = 0; i < sheets.size(); ++i) {
        int columns = 0;
        for (const QStringList &row : sheets[i].rows)
            columns = std::max(columns, int(row.size()));
        if (sheets[i].rows.size() > MaxRows || columns > MaxColumns) {
            *error = QObject::tr("Il foglio \"%1\" supera i limiti di Excel (%2 righe, %3 colonne)")
                         .arg(names[i], QString::number(MaxRows), QString::number(MaxColumns));
            return false;
        }
    }

    Styles styles;
    QList<QByteArray> parts;
    QStringList filterRanges;
    int truncated = 0;
    for (qsizetype i = 0; i < sheets.size(); ++i) {
        QString range;
        parts << sheetXml(sheets[i], i == 0, styles, &truncated, &range);
        filterRanges << range;
    }

    QString types = QString::fromLatin1(kXmlHeader) +
                    QStringLiteral("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                                   "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package."
                                   "relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                                   "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd."
                                   "openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                                   "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd."
                                   "openxmlformats-officedocument.spreadsheetml.styles+xml\"/>");
    QString workbookRels = QString::fromLatin1(kXmlHeader) +
                           QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (qsizetype i = 1; i <= parts.size(); ++i) {
        types += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd."
                                "openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                     .arg(i);
        workbookRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"%2/worksheet\" Target=\"worksheets/sheet%1.xml\"/>")
                            .arg(QString::number(i), QLatin1String(kDocRelNs));
    }
    types += QStringLiteral("</Types>");
    workbookRels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"%2/styles\" Target=\"styles.xml\"/></Relationships>")
                        .arg(QString::number(parts.size() + 1), QLatin1String(kDocRelNs));
    const QString rootRels = QString::fromLatin1(kXmlHeader) +
                             QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
                                            "relationships\"><Relationship Id=\"rId1\" Type=\"%1/officeDocument\" "
                                            "Target=\"xl/workbook.xml\"/></Relationships>")
                                 .arg(QLatin1String(kDocRelNs));

    // Built in memory and saved with QSaveFile, as in the QDPX export: QZipWriter::close() closes
    // its device, which QSaveFile forbids.
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QZipWriter zip(&buffer);
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    zip.addFile(QStringLiteral("[Content_Types].xml"), types.toUtf8());
    zip.addFile(QStringLiteral("_rels/.rels"), rootRels.toUtf8());
    zip.addFile(QStringLiteral("xl/workbook.xml"), workbookXml(names, filterRanges));
    zip.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"), workbookRels.toUtf8());
    zip.addFile(QStringLiteral("xl/styles.xml"), styles.xml());
    for (qsizetype i = 0; i < parts.size(); ++i)
        zip.addFile(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), parts[i]);
    zip.close();
    QSaveFile file(path);
    if (zip.status() != QZipWriter::NoError || !file.open(QIODevice::WriteOnly)
        || file.write(buffer.data()) != buffer.data().size() || !file.commit()) {
        *error = QObject::tr("Impossibile scrivere %1").arg(path);
        return false;
    }
    if (truncatedCells)
        *truncatedCells = truncated;
    return true;
}

} // namespace Xlsx
