#include "core/Exporters.h"
#include "core/Images.h"
#include "core/Pdfs.h"
#include "core/Project.h"
#include "core/TextUtils.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <functional>
#include <memory>

namespace Exporters {

namespace {

// Spreadsheets run a cell starting with = + - @ as a formula: imported text could carry one
// (=HYPERLINK, DDE). Such cells get a leading apostrophe, unless they are plain numbers.
bool looksLikeFormula(const QString &s)
{
    if (s.isEmpty())
        return false;
    const QChar c = s.front();
    if (c != QLatin1Char('=') && c != QLatin1Char('+') && c != QLatin1Char('-') && c != QLatin1Char('@')
        && c != QLatin1Char('\t') && c != QLatin1Char('\r'))
        return false;
    static const QRegularExpression number(QStringLiteral("^[+-]?\\d+([.,]\\d+)?$"));
    return !number.match(s).hasMatch();
}

QString csvField(QString s)
{
    if (looksLikeFormula(s))
        s.prepend(QLatin1Char('\''));
    const bool quote = s.contains(QLatin1Char(';')) || s.contains(QLatin1Char('"')) ||
                       s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char(',')) || s.startsWith(QLatin1Char(' '));
    if (!quote)
        return s;
    s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

bool writeUtf8(const QString &path, const QString &content, bool bom, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = f.errorString();
        return false;
    }
    if (bom)
        f.write("\xEF\xBB\xBF");
    f.write(content.toUtf8());
    if (!f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

QString dateString(const QDateTime &dt, bool iso = false)
{
    if (!dt.isValid())
        return {};
    return iso ? dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
               : QLocale().toString(dt.toLocalTime(), QLocale::ShortFormat);
}

const char *const kHtmlStyle =
    "body{font-family:Segoe UI,Arial,sans-serif;margin:24px;color:#222}"
    "h1{font-size:20px}table{border-collapse:collapse;font-size:13px}"
    "td,th{border:1px solid #ccc;padding:4px 8px;vertical-align:top;text-align:left}"
    "th{background:#f2f2f2}.seg{border-left:4px solid #888;padding:6px 10px;margin:10px 0;background:#fafafa}"
    ".meta{font-size:12px;color:#555;margin-bottom:4px}.code{font-weight:600}"
    ".text{white-space:pre-wrap}.area img{max-width:100%;border:1px solid #ccc}.comment{font-size:12px;color:#444;font-style:italic;margin-top:4px}";

} // namespace

QString htmlEscape(const QString &s) { return s.toHtmlEscaped(); }

bool writeCsv(const QString &path, const Table &rows, QString *error)
{
    QString out;
    for (const QStringList &row : rows) {
        QStringList fields;
        fields.reserve(row.size());
        for (const QString &f : row)
            fields << csvField(f);
        out += fields.join(QLatin1Char(';')) + QStringLiteral("\r\n");
    }
    return writeUtf8(path, out, true, error);
}

Table readCsv(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return {};
    }
    QByteArray data = f.readAll();
    QString text;
    if (data.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(data.mid(3));
    } else {
        QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        text = utf8.decode(data);
        if (utf8.hasError())
            text = QStringDecoder(QStringDecoder::System).decode(data);
    }
    text = TextUtils::normalizeText(text);

    const QString firstLine = text.section(QLatin1Char('\n'), 0, 0);
    QChar sep = QLatin1Char(';');
    if (firstLine.count(QLatin1Char('\t')) > firstLine.count(sep))
        sep = QLatin1Char('\t');
    else if (firstLine.count(QLatin1Char(',')) > firstLine.count(sep))
        sep = QLatin1Char(',');

    Table rows;
    QStringList row;
    QString field;
    bool inQuotes = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += c;
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == sep) {
            row << field;
            field.clear();
        } else if (c == QLatin1Char('\n')) {
            row << field;
            field.clear();
            rows << row;
            row.clear();
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows << row;
    }
    // Undo the apostrophe writeCsv puts before formula-like cells.
    for (QStringList &r : rows)
        for (QString &f : r)
            if (f.startsWith(QLatin1Char('\'')) && looksLikeFormula(f.mid(1)))
                f.remove(0, 1);
    return rows;
}

bool writeHtmlTable(const QString &path, const QString &title, const Table &rows, QString *error)
{
    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title><style>%2</style>"
                                  "</head><body><h1>%1</h1><table>")
                       .arg(htmlEscape(title), QString::fromLatin1(kHtmlStyle));
    for (qsizetype r = 0; r < rows.size(); ++r) {
        html += QStringLiteral("<tr>");
        for (const QString &cell : rows[r]) {
            const QString tag = r == 0 ? QStringLiteral("th") : QStringLiteral("td");
            html += QLatin1Char('<') + tag + QLatin1Char('>') + htmlEscape(cell).replace(QLatin1Char('\n'), QStringLiteral("<br>")) +
                    QStringLiteral("</") + tag + QLatin1Char('>');
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table></body></html>");
    return writeUtf8(path, html, false, error);
}

Table segmentsTable(const Project &project, const QList<Segment> &segments, bool isoDates)
{
    QHash<qint64, QString> groupNames;
    for (const DocumentGroup &g : project.groups())
        groupNames.insert(g.id, g.name);
    QHash<qint64, Document> docs;
    for (const Document &d : project.documents())
        docs.insert(d.id, d);
    QHash<qint64, QString> texts;
    QHash<qint64, QString> codePaths;

    Table rows;
    rows << QStringList{QObject::tr("Gruppo"), QObject::tr("Documento"), QObject::tr("Codice"), QObject::tr("Inizio"),
                        QObject::tr("Fine"), QObject::tr("Peso"), QObject::tr("Commento"), QObject::tr("Autore"),
                        QObject::tr("Creato"), QObject::tr("Segmento")};
    for (const Segment &s : segments) {
        if (!texts.contains(s.documentId))
            texts.insert(s.documentId, project.documentText(s.documentId));
        if (!codePaths.contains(s.codeId))
            codePaths.insert(s.codeId, project.codePath(s.codeId));
        const Document d = docs.value(s.documentId);
        if (s.kind == SegmentKind::Area) { // no text positions: the area is described instead
            rows << QStringList{groupNames.value(d.groupId), d.name, codePaths.value(s.codeId), QString(), QString(),
                                QString::number(s.weight), s.comment, s.author, dateString(s.createdAt, isoDates),
                                s.areaText.isEmpty() ? Images::areaLabel(s) : s.areaText};
            continue;
        }
        rows << QStringList{groupNames.value(d.groupId), d.name, codePaths.value(s.codeId), QString::number(s.start),
                            QString::number(s.end), QString::number(s.weight), s.comment, s.author,
                            dateString(s.createdAt, isoDates), texts.value(s.documentId).mid(s.start, s.length())};
    }
    return rows;
}

bool exportSegmentsHtml(const Project &project, const QList<Segment> &segments, const QString &path, QString *error)
{
    QHash<qint64, Document> docs;
    for (const Document &d : project.documents())
        docs.insert(d.id, d);
    QHash<qint64, Code> codes;
    for (const Code &c : project.codes())
        codes.insert(c.id, c);
    QHash<qint64, QString> texts;
    QHash<qint64, QImage> images; // decoded once per image document
    QHash<qint64, std::shared_ptr<Pdfs::Doc>> pdfs; // loaded once per PDF document

    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title><style>%2</style>"
                                  "</head><body><h1>%1</h1><p>%3</p>")
                       .arg(htmlEscape(QObject::tr("Segmenti recuperati – %1").arg(project.name())),
                            QString::fromLatin1(kHtmlStyle),
                            htmlEscape(QObject::tr("%n segmenti", nullptr, int(segments.size()))));
    for (const Segment &s : segments) {
        const Code c = codes.value(s.codeId);
        if (s.kind == SegmentKind::Area) {
            // The cropped area travels inside the page as a PNG data URI.
            QImage crop;
            if (s.page > 0) { // PDF area: rendered from the page
                if (!pdfs.contains(s.documentId)) {
                    auto doc = std::make_shared<Pdfs::Doc>();
                    doc->load(project.documentData(s.documentId));
                    pdfs.insert(s.documentId, doc);
                }
                crop = pdfs.value(s.documentId)->cropArea(s, 900, 700);
            } else {
                if (!images.contains(s.documentId))
                    images.insert(s.documentId, project.documentImage(s.documentId));
                crop = Images::scaledToFit(Images::crop(images.value(s.documentId), s), 900, 700);
            }
            html += QStringLiteral("<div class=\"seg\" style=\"border-left-color:%1\"><div class=\"meta\">"
                                   "<span class=\"code\">%2</span> · %3 · %4%5</div><div class=\"area\">%6</div>")
                        .arg(c.color.name(), htmlEscape(project.codePath(s.codeId)), htmlEscape(docs.value(s.documentId).name),
                             htmlEscape(Images::areaLabel(s)),
                             s.weight ? QStringLiteral(" · ") + QObject::tr("peso %1").arg(s.weight) : QString(),
                             crop.isNull() ? QString()
                                           : QStringLiteral("<img alt=\"\" src=\"data:image/png;base64,") +
                                                 QString::fromLatin1(Images::toPng(crop).toBase64()) +
                                                 QStringLiteral("\">"));
            if (!s.areaText.isEmpty())
                html += QStringLiteral("<div class=\"text\">") + htmlEscape(s.areaText) + QStringLiteral("</div>");
            if (!s.comment.isEmpty())
                html += QStringLiteral("<div class=\"comment\">") + htmlEscape(s.comment) + QStringLiteral("</div>");
            html += QStringLiteral("</div>");
            continue;
        }
        if (!texts.contains(s.documentId))
            texts.insert(s.documentId, project.documentText(s.documentId));
        // One multi-argument arg(): chained calls would also replace a "%5" typed in a name.
        html += QStringLiteral("<div class=\"seg\" style=\"border-left-color:%1\"><div class=\"meta\">"
                               "<span class=\"code\">%2</span> · %3 · %4–%5%6</div><div class=\"text\">%7</div>")
                    .arg(c.color.name(), htmlEscape(project.codePath(s.codeId)), htmlEscape(docs.value(s.documentId).name),
                         QString::number(s.start), QString::number(s.end),
                         s.weight ? QStringLiteral(" · ") + QObject::tr("peso %1").arg(s.weight) : QString(),
                         htmlEscape(texts.value(s.documentId).mid(s.start, s.length())));
        if (!s.comment.isEmpty())
            html += QStringLiteral("<div class=\"comment\">") + htmlEscape(s.comment) + QStringLiteral("</div>");
        html += QStringLiteral("</div>");
    }
    html += QStringLiteral("</body></html>");
    return writeUtf8(path, html, false, error);
}

Table codebookTable(const Project &project)
{
    const QList<Code> codes = project.codes();
    const QHash<qint64, int> counts = project.segmentCountByCode();
    const QHash<qint64, qint64> memoIds = project.memoIdsFor(MemoTarget::Code);
    QHash<qint64, QList<Code>> children;
    for (const Code &c : codes)
        children[c.parentId] << c;

    Table rows;
    rows << QStringList{QObject::tr("Codice"), QObject::tr("Livello"), QObject::tr("Colore"), QObject::tr("Descrizione"),
                        QObject::tr("Segmenti"), QObject::tr("Memo")};
    std::function<void(qint64, int, const QString &)> walk = [&](qint64 parent, int level, const QString &prefix) {
        for (const Code &c : children.value(parent)) {
            const QString path = prefix.isEmpty() ? c.name : prefix + QStringLiteral(" > ") + c.name;
            const QString memo = memoIds.contains(c.id) ? project.memo(memoIds.value(c.id)).body : QString();
            rows << QStringList{path, QString::number(level), c.color.name(), c.description,
                                QString::number(counts.value(c.id)), memo};
            walk(c.id, level + 1, path);
        }
    };
    walk(0, 1, {});
    return rows;
}

bool exportCodebookHtml(const Project &project, const QString &path, QString *error)
{
    return writeHtmlTable(path, QObject::tr("Codebook – %1").arg(project.name()), codebookTable(project), error);
}

Table variablesTable(const Project &project)
{
    const QList<Variable> vars = project.variables();
    const auto values = project.allVariableValues();
    QHash<qint64, QString> groupNames;
    for (const DocumentGroup &g : project.groups())
        groupNames.insert(g.id, g.name);

    Table rows;
    QStringList header{QObject::tr("Documento"), QObject::tr("Gruppo")};
    for (const Variable &v : vars)
        header << v.name;
    rows << header;
    for (const Document &d : project.documents()) {
        QStringList row{d.name, groupNames.value(d.groupId)};
        for (const Variable &v : vars)
            row << values.value(d.id).value(v.id);
        rows << row;
    }
    return rows;
}

Table documentsTable(const Project &project, bool isoDates)
{
    QHash<qint64, QString> groupNames;
    for (const DocumentGroup &g : project.groups())
        groupNames.insert(g.id, g.name);
    QHash<qint64, QStringList> setNames;
    for (const DocumentSet &s : project.documentSets())
        for (qint64 doc : project.documentSetMembers(s.id))
            setNames[doc] << s.name;
    const QHash<qint64, int> segmentCounts = project.segmentCountByDocument();

    Table rows;
    rows << QStringList{QObject::tr("Documento"), QObject::tr("Gruppo"), QObject::tr("Set"), QObject::tr("Segmenti"),
                        QObject::tr("Caratteri"), QObject::tr("Importato"), QObject::tr("File di origine")};
    for (const Document &d : project.documents())
        rows << QStringList{d.name, groupNames.value(d.groupId), setNames.value(d.id).join(QStringLiteral("; ")),
                            QString::number(segmentCounts.value(d.id)),
                            QString::number(project.documentText(d.id).size()), dateString(d.importedAt, isoDates),
                            d.sourcePath};
    return rows;
}

QString memoTargetLabel(const Project &project, MemoTarget type, qint64 id)
{
    switch (type) {
    case MemoTarget::Project:
        return QObject::tr("Progetto");
    case MemoTarget::Free:
        return QObject::tr("Memo libero");
    case MemoTarget::Group:
        for (const DocumentGroup &g : project.groups())
            if (g.id == id)
                return QObject::tr("Gruppo: %1").arg(g.name);
        return QObject::tr("Gruppo");
    case MemoTarget::Document:
        return QObject::tr("Documento: %1").arg(project.document(id).name);
    case MemoTarget::Code:
        return QObject::tr("Codice: %1").arg(project.codePath(id));
    case MemoTarget::Segment: {
        const Segment s = project.segment(id);
        return QObject::tr("Segmento: %1 / %2 – \"%3\"")
            .arg(project.document(s.documentId).name, project.code(s.codeId).name,
                 s.kind == SegmentKind::Text ? TextUtils::excerpt(project.documentText(s.documentId), s.start, s.end, 50)
                                             : !s.areaText.isEmpty() ? TextUtils::excerpt(s.areaText, 0, int(s.areaText.size()), 50)
                                                                     : Images::areaLabel(s));
    }
    case MemoTarget::DocumentSet:
        for (const DocumentSet &s : project.documentSets())
            if (s.id == id)
                return QObject::tr("Set: %1").arg(s.name);
        return QObject::tr("Set");
    }
    return {};
}

QString memoLinksLabel(const Project &project, const Memo &memo)
{
    QStringList labels;
    if (memo.targetType == MemoTarget::Project)
        labels << memoTargetLabel(project, MemoTarget::Project, 0);
    for (const MemoLink &l : project.memoLinks(memo.id))
        labels << memoTargetLabel(project, l.targetType, l.targetId);
    if (labels.isEmpty())
        labels << memoTargetLabel(project, MemoTarget::Free, 0);
    return labels.join(QStringLiteral("; "));
}

Table memosTable(const Project &project, bool isoDates)
{
    Table rows{{QObject::tr("Collegato a"), QObject::tr("Titolo"), QObject::tr("Testo"), QObject::tr("Autore"),
                QObject::tr("Creato"), QObject::tr("Modificato")}};
    for (const Memo &m : project.memos())
        rows << QStringList{memoLinksLabel(project, m), m.title, m.body, m.author, dateString(m.createdAt, isoDates),
                            dateString(m.modifiedAt, isoDates)};
    return rows;
}

Xlsx::Sheet segmentsSheet(const Project &project, const QList<Segment> &segments)
{
    QHash<qint64, QColor> colors;
    for (const Code &c : project.codes())
        colors.insert(c.id, c.color);
    Xlsx::Sheet sheet;
    sheet.name = QObject::tr("Segmenti");
    sheet.rows = segmentsTable(project, segments, true);
    sheet.textColumns = {0, 1, 2, 6, 7, 9};
    sheet.wrapColumns = {6, 9};
    for (qsizetype i = 0; i < segments.size(); ++i)
        sheet.fills.insert({int(i) + 1, 2}, colors.value(segments[i].codeId));
    return sheet;
}

Xlsx::Sheet codebookSheet(const Project &project)
{
    Xlsx::Sheet sheet;
    sheet.name = QObject::tr("Codici");
    sheet.rows = codebookTable(project);
    sheet.textColumns = {0, 2, 3, 5};
    sheet.wrapColumns = {3, 5};
    for (qsizetype r = 1; r < sheet.rows.size(); ++r) {
        const QColor color = QColor::fromString(sheet.rows[r].value(2));
        if (color.isValid()) {
            sheet.fills.insert({int(r), 0}, color);
            sheet.fills.insert({int(r), 2}, color);
        }
    }
    return sheet;
}

Xlsx::Sheet variablesSheet(const Project &project)
{
    Xlsx::Sheet sheet;
    sheet.name = QObject::tr("Variabili");
    sheet.rows = variablesTable(project);
    sheet.textColumns = {0, 1};
    const QList<Variable> vars = project.variables();
    for (qsizetype i = 0; i < vars.size(); ++i)
        if (vars[i].type == VariableType::Text) // "007" or "2024" typed as text stays text
            sheet.textColumns.insert(int(i) + 2);
    return sheet;
}

Xlsx::Sheet documentsSheet(const Project &project)
{
    Xlsx::Sheet sheet;
    sheet.name = QObject::tr("Documenti");
    sheet.rows = documentsTable(project, true);
    sheet.textColumns = {0, 1, 2, 6};
    return sheet;
}

Xlsx::Sheet memosSheet(const Project &project)
{
    Xlsx::Sheet sheet;
    sheet.name = QObject::tr("Memo");
    sheet.rows = memosTable(project, true);
    sheet.textColumns = {0, 1, 2, 3};
    sheet.wrapColumns = {0, 2};
    return sheet;
}

Xlsx::Sheet tableSheet(const QString &name, const Table &rows)
{
    Xlsx::Sheet sheet;
    sheet.name = name;
    sheet.rows = rows;
    sheet.textColumns = {0};
    return sheet;
}

QList<Xlsx::Sheet> projectSheets(const Project &project)
{
    return {documentsSheet(project), codebookSheet(project), segmentsSheet(project, project.allSegments(true)),
            variablesSheet(project), memosSheet(project)};
}

namespace {

// "1.234,5", "1,234.5", "12,5", "12.5"; a separator is a thousands one only when followed by
// groups of exactly three digits, or when the other separator comes later.
bool parseDecimal(QString s, double *out)
{
    s.remove(QLatin1Char(' ')).remove(QChar(0x00A0));
    static const QRegularExpression shape(QStringLiteral("^[+-]?\\d[\\d.,]*$"));
    if (!shape.match(s).hasMatch())
        return false;
    const qsizetype dots = s.count(QLatin1Char('.'));
    const qsizetype commas = s.count(QLatin1Char(','));
    QChar decimalSep, groupSep; // null when absent
    if (dots && commas) {
        const bool dotLast = s.lastIndexOf(QLatin1Char('.')) > s.lastIndexOf(QLatin1Char(','));
        decimalSep = QLatin1Char(dotLast ? '.' : ',');
        groupSep = QLatin1Char(dotLast ? ',' : '.');
    } else if (dots || commas) {
        const QChar sep = QLatin1Char(dots ? '.' : ',');
        if (std::max(dots, commas) == 1)
            decimalSep = sep;
        else
            groupSep = sep;
    }
    QString integerPart = s;
    QString fraction;
    if (!decimalSep.isNull()) {
        if (s.count(decimalSep) != 1)
            return false;
        const qsizetype pos = s.indexOf(decimalSep);
        integerPart = s.left(pos);
        fraction = s.mid(pos + 1);
        static const QRegularExpression digitsOnly(QStringLiteral("^\\d+$"));
        if (!digitsOnly.match(fraction).hasMatch())
            return false;
    }
    if (!groupSep.isNull()) {
        const QRegularExpression grouped(QStringLiteral("^[+-]?\\d{1,3}(\\%1\\d{3})+$").arg(groupSep));
        if (!grouped.match(integerPart).hasMatch())
            return false;
        integerPart.remove(groupSep);
    }
    QString digits = integerPart;
    if (!fraction.isEmpty())
        digits += QLatin1Char('.') + fraction;
    bool ok = false;
    const double d = digits.toDouble(&ok);
    if (ok)
        *out = d;
    return ok;
}

QDate parseDate(const QString &v, bool lenient)
{
    QDate d = QDate::fromString(v, Qt::ISODate);
    QStringList formats{QStringLiteral("dd/MM/yyyy"), QStringLiteral("d/M/yyyy")};
    if (lenient)
        formats << QStringLiteral("dd.MM.yyyy") << QStringLiteral("dd-MM-yyyy") << QStringLiteral("d/M/yy");
    for (const QString &fmt : std::as_const(formats)) {
        if (d.isValid())
            break;
        d = QDate::fromString(v, fmt);
    }
    return d;
}

// Boolean words recognised when guessing a column's type ("x" is accepted when typing, not guessed).
bool booleanWord(const QString &lower, bool *value)
{
    static const QStringList yes{QStringLiteral("1"), QStringLiteral("sì"), QStringLiteral("si"),
                                 QStringLiteral("yes"), QStringLiteral("true"), QStringLiteral("vero")};
    static const QStringList no{QStringLiteral("0"), QStringLiteral("no"), QStringLiteral("false"),
                                QStringLiteral("falso")};
    if (yes.contains(lower)) {
        *value = true;
        return true;
    }
    if (no.contains(lower)) {
        *value = false;
        return true;
    }
    return false;
}

bool isSkippedColumn(const QString &name)
{
    const QString lower = name.toLower();
    return name.isEmpty() || lower == QLatin1String("gruppo") || lower == QLatin1String("group");
}

struct CsvLayout {
    Table rows;
    int docColumn = 0;
};

bool loadCsvLayout(const QString &path, CsvLayout *layout, QString *error)
{
    layout->rows = readCsv(path, error);
    if (layout->rows.size() < 2) {
        if (error->isEmpty())
            *error = QObject::tr("Il file non contiene righe di dati");
        return false;
    }
    const QStringList &header = layout->rows.first();
    for (int i = 0; i < header.size(); ++i) {
        const QString h = header[i].trimmed().toLower();
        if (h == QLatin1String("documento") || h == QLatin1String("document") || h == QLatin1String("nome documento")) {
            layout->docColumn = i;
            break;
        }
    }
    return true;
}

QStringList columnValues(const Table &rows, int column)
{
    QStringList values;
    for (qsizetype r = 1; r < rows.size(); ++r) {
        if (column < rows[r].size() && !rows[r][column].trimmed().isEmpty())
            values << rows[r][column].trimmed();
    }
    return values;
}

} // namespace

bool normalizeVariableValue(VariableType type, const QString &input, QString *out)
{
    const QString v = input.trimmed();
    out->clear();
    if (v.isEmpty())
        return true;
    switch (type) {
    case VariableType::Text:
        *out = v;
        return true;
    case VariableType::Integer: {
        static const QRegularExpression integer(QStringLiteral("^[+-]?\\d+$"));
        bool ok = false;
        const qlonglong n = integer.match(v).hasMatch() ? v.toLongLong(&ok) : 0;
        if (ok)
            *out = QString::number(n);
        return ok;
    }
    case VariableType::Decimal: {
        double d = 0;
        if (!parseDecimal(v, &d))
            return false;
        *out = QString::number(d, 'g', 15);
        return true;
    }
    case VariableType::Boolean: {
        const QString l = v.toLower();
        bool b = false;
        if (booleanWord(l, &b) || l == QLatin1String("x")) {
            *out = (b || l == QLatin1String("x")) ? QStringLiteral("1") : QStringLiteral("0");
            return true;
        }
        return false;
    }
    case VariableType::Date: {
        const QDate d = parseDate(v, true);
        if (!d.isValid())
            return false;
        *out = d.toString(Qt::ISODate);
        return true;
    }
    }
    return false;
}

VariableType inferVariableType(const QStringList &values)
{
    QStringList filled;
    for (const QString &s : values) {
        if (!s.trimmed().isEmpty())
            filled << s.trimmed();
    }
    if (filled.isEmpty())
        return VariableType::Text;

    const auto all = [&filled](const auto &pred) {
        return std::all_of(filled.cbegin(), filled.cend(), pred);
    };

    // Boolean: at most two distinct values; a bare 0/1 column needs both to not be taken for integers.
    QSet<QString> distinct;
    bool booleanOk = true;
    bool onlyDigits = true;
    for (const QString &s : std::as_const(filled)) {
        const QString l = s.toLower();
        bool b = false;
        if (!booleanWord(l, &b)) {
            booleanOk = false;
            break;
        }
        distinct.insert(l);
        onlyDigits = onlyDigits && (l == QLatin1String("0") || l == QLatin1String("1"));
    }
    if (booleanOk && distinct.size() <= 2 && (!onlyDigits || distinct.size() == 2))
        return VariableType::Boolean;

    QString tmp;
    if (all([&tmp](const QString &s) { return normalizeVariableValue(VariableType::Integer, s, &tmp); }))
        return VariableType::Integer;
    if (all([&tmp](const QString &s) { return normalizeVariableValue(VariableType::Decimal, s, &tmp); }))
        return VariableType::Decimal;
    if (all([](const QString &s) { return parseDate(s, false).isValid(); }))
        return VariableType::Date;
    return VariableType::Text;
}

bool previewVariablesCsv(const Project &project, const QString &path, CsvVariablesPreview *preview, QString *error)
{
    CsvLayout layout;
    if (!loadCsvLayout(path, &layout, error))
        return false;
    const Table &rows = layout.rows;
    const QStringList &header = rows.first();

    QHash<QString, Variable> varsByName;
    for (const Variable &v : project.variables())
        varsByName.insert(v.name, v);

    *preview = {};
    for (int i = 0; i < header.size(); ++i) {
        const QString name = header[i].trimmed();
        if (i == layout.docColumn || isSkippedColumn(name))
            continue;
        CsvVariableColumn col;
        col.name = name;
        const QStringList values = columnValues(rows, i);
        const auto existing = varsByName.constFind(name);
        if (existing != varsByName.cend()) {
            col.existing = true;
            col.type = existing->type;
            QString tmp;
            for (const QString &s : values) {
                if (!normalizeVariableValue(col.type, s, &tmp))
                    ++col.invalidCount;
            }
        } else {
            col.type = inferVariableType(values);
        }
        col.samples = values.mid(0, 3);
        preview->columns << col;
    }

    QSet<QString> docNames;
    for (const Document &d : project.documents())
        docNames.insert(d.name);
    for (qsizetype r = 1; r < rows.size(); ++r) {
        const QStringList &row = rows[r];
        if (row.size() <= layout.docColumn || row[layout.docColumn].trimmed().isEmpty())
            continue;
        const QString name = row[layout.docColumn].trimmed();
        if (docNames.contains(name))
            ++preview->matchedRows;
        else
            preview->unmatchedDocuments << name;
    }
    preview->sampleRows = rows.mid(0, 6);
    return true;
}

bool importVariablesCsv(Project &project, const QString &path, QString *report, QString *error,
                        const QHash<QString, VariableType> &types)
{
    CsvLayout layout;
    if (!loadCsvLayout(path, &layout, error))
        return false;
    const Table &rows = layout.rows;
    const QStringList &header = rows.first();

    QHash<QString, qint64> docsByName;
    for (const Document &d : project.documents()) {
        if (!docsByName.contains(d.name))
            docsByName.insert(d.name, d.id);
    }
    QHash<QString, Variable> varsByName;
    for (const Variable &v : project.variables())
        varsByName.insert(v.name, v);

    project.setUndoLabel(QObject::tr("Importa variabili"));
    project.beginBatch();
    QHash<int, Variable> columnVar;
    for (int i = 0; i < header.size(); ++i) {
        const QString name = header[i].trimmed();
        if (i == layout.docColumn || isSkippedColumn(name))
            continue;
        Variable var = varsByName.value(name);
        if (!var.id) {
            var.name = name;
            var.type = types.contains(name) ? types.value(name) : inferVariableType(columnValues(rows, i));
            var.id = project.addVariable(name, var.type);
        }
        if (var.id)
            columnVar.insert(i, var);
    }
    int matched = 0;
    QStringList missing;
    QStringList invalid;
    for (qsizetype r = 1; r < rows.size(); ++r) {
        const QStringList &row = rows[r];
        if (row.size() <= layout.docColumn || row[layout.docColumn].trimmed().isEmpty())
            continue;
        const QString docName = row[layout.docColumn].trimmed();
        const qint64 docId = docsByName.value(docName);
        if (!docId) {
            missing << docName;
            continue;
        }
        ++matched;
        for (auto it = columnVar.cbegin(); it != columnVar.cend(); ++it) {
            if (it.key() >= row.size())
                continue;
            QString value;
            if (!normalizeVariableValue(it->type, row[it.key()], &value)) {
                invalid << QObject::tr("%1 / %2: \"%3\"").arg(docName, it->name, row[it.key()].trimmed());
                continue;
            }
            project.setVariableValue(docId, it->id, value);
        }
    }
    project.commitBatch();
    *report = QObject::tr("%1 documenti aggiornati, %2 variabili.").arg(matched).arg(columnVar.size());
    if (!missing.isEmpty())
        *report += QLatin1Char('\n') + QObject::tr("Documenti non trovati: %1").arg(missing.join(QStringLiteral(", ")));
    if (!invalid.isEmpty())
        *report += QLatin1Char('\n') + QObject::tr("Valori non compatibili con il tipo (ignorati): %1")
                                           .arg(invalid.join(QStringLiteral("; ")));
    return true;
}

} // namespace Exporters
