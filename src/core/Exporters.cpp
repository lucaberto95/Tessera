#include "core/Exporters.h"
#include "core/Project.h"
#include "core/TextUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSaveFile>
#include <QStringDecoder>

#include <functional>

namespace Exporters {

namespace {

QString csvField(QString s)
{
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

QString dateString(const QDateTime &dt)
{
    return dt.isValid() ? QLocale().toString(dt.toLocalTime(), QLocale::ShortFormat) : QString();
}

const char *const kHtmlStyle =
    "body{font-family:Segoe UI,Arial,sans-serif;margin:24px;color:#222}"
    "h1{font-size:20px}table{border-collapse:collapse;font-size:13px}"
    "td,th{border:1px solid #ccc;padding:4px 8px;vertical-align:top;text-align:left}"
    "th{background:#f2f2f2}.seg{border-left:4px solid #888;padding:6px 10px;margin:10px 0;background:#fafafa}"
    ".meta{font-size:12px;color:#555;margin-bottom:4px}.code{font-weight:600}"
    ".text{white-space:pre-wrap}.comment{font-size:12px;color:#444;font-style:italic;margin-top:4px}";

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

Table segmentsTable(const Project &project, const QList<Segment> &segments)
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
        rows << QStringList{groupNames.value(d.groupId), d.name, codePaths.value(s.codeId), QString::number(s.start),
                            QString::number(s.end), QString::number(s.weight), s.comment, s.author,
                            dateString(s.createdAt), texts.value(s.documentId).mid(s.start, s.length())};
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

    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title><style>%2</style>"
                                  "</head><body><h1>%1</h1><p>%3</p>")
                       .arg(htmlEscape(QObject::tr("Segmenti recuperati – %1").arg(project.name())),
                            QString::fromLatin1(kHtmlStyle),
                            htmlEscape(QObject::tr("%n segmenti", nullptr, int(segments.size()))));
    for (const Segment &s : segments) {
        if (!texts.contains(s.documentId))
            texts.insert(s.documentId, project.documentText(s.documentId));
        const Code c = codes.value(s.codeId);
        html += QStringLiteral("<div class=\"seg\" style=\"border-left-color:%1\"><div class=\"meta\">"
                               "<span class=\"code\">%2</span> · %3 · %4–%5%6</div><div class=\"text\">%7</div>")
                    .arg(c.color.name(), htmlEscape(project.codePath(s.codeId)), htmlEscape(docs.value(s.documentId).name))
                    .arg(s.start)
                    .arg(s.end)
                    .arg(s.weight ? QStringLiteral(" · ") + QObject::tr("peso %1").arg(s.weight) : QString())
                    .arg(htmlEscape(texts.value(s.documentId).mid(s.start, s.length())));
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

bool importVariablesCsv(Project &project, const QString &path, QString *report, QString *error)
{
    const Table rows = readCsv(path, error);
    if (rows.size() < 2) {
        if (error->isEmpty())
            *error = QObject::tr("Il file non contiene righe di dati");
        return false;
    }
    const QStringList header = rows.first();
    int docColumn = 0;
    for (int i = 0; i < header.size(); ++i) {
        const QString h = header[i].trimmed().toLower();
        if (h == QLatin1String("documento") || h == QLatin1String("document") || h == QLatin1String("nome documento")) {
            docColumn = i;
            break;
        }
    }
    QHash<QString, qint64> docsByName;
    for (const Document &d : project.documents()) {
        if (!docsByName.contains(d.name))
            docsByName.insert(d.name, d.id);
    }
    QHash<QString, qint64> varsByName;
    for (const Variable &v : project.variables())
        varsByName.insert(v.name, v.id);

    project.setUndoLabel(QObject::tr("Importa variabili"));
    project.beginBatch();
    QHash<int, qint64> columnVar;
    for (int i = 0; i < header.size(); ++i) {
        const QString name = header[i].trimmed();
        const QString lower = name.toLower();
        if (i == docColumn || name.isEmpty() || lower == QLatin1String("gruppo") || lower == QLatin1String("group"))
            continue;
        qint64 id = varsByName.value(name);
        if (!id)
            id = project.addVariable(name, VariableType::Text);
        if (id)
            columnVar.insert(i, id);
    }
    int matched = 0;
    QStringList missing;
    for (qsizetype r = 1; r < rows.size(); ++r) {
        const QStringList &row = rows[r];
        if (row.size() <= docColumn || row[docColumn].trimmed().isEmpty())
            continue;
        const qint64 docId = docsByName.value(row[docColumn].trimmed());
        if (!docId) {
            missing << row[docColumn].trimmed();
            continue;
        }
        ++matched;
        for (auto it = columnVar.cbegin(); it != columnVar.cend(); ++it) {
            if (it.key() < row.size())
                project.setVariableValue(docId, it.value(), row[it.key()].trimmed());
        }
    }
    project.commitBatch();
    *report = QObject::tr("%1 documenti aggiornati, %2 variabili.").arg(matched).arg(columnVar.size());
    if (!missing.isEmpty())
        *report += QLatin1Char('\n') + QObject::tr("Documenti non trovati: %1").arg(missing.join(QStringLiteral(", ")));
    return true;
}

} // namespace Exporters
