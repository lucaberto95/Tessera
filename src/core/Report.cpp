#include "core/Report.h"
#include "core/Project.h"
#include "core/Summaries.h"

#include <QBuffer>
#include <QDate>
#include <QFile>
#include <QHash>
#include <QLocale>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextDocument>
#include <QtCore/private/qzipwriter_p.h>

#include <algorithm>
#include <functional>

namespace Report {

namespace {

constexpr int kExampleMaxChars = 400;

Run run(const QString &text, bool bold = false, bool italic = false, const QColor &color = QColor())
{
    return Run{text, bold, italic, color};
}

Block block(BlockType type, const QList<Run> &runs, int level = 1)
{
    Block b;
    b.type = type;
    b.runs = runs;
    b.level = level;
    return b;
}

Block heading(const QString &text, int level)
{
    Block b = block(BlockType::Heading, {run(text)}, level);
    b.pageBreakBefore = level == 1;
    return b;
}

QString cut(const QString &text, int max)
{
    const QString t = text.trimmed();
    return t.size() <= max ? t : t.left(max).trimmed() + QStringLiteral("…");
}

QString dateLabel(const QDateTime &dt)
{
    return dt.isValid() ? QLocale().toString(dt, QLocale::ShortFormat) : QString();
}

// Everything the builder needs about segments, computed once.
struct SegmentSource {
    const Project &project;
    QHash<qint64, ::Document> docs;
    QHash<qint64, int> docOrder;
    QHash<qint64, QString> texts;

    explicit SegmentSource(const Project &p) : project(p)
    {
        int i = 0;
        for (const ::Document &d : p.documents()) {
            docs.insert(d.id, d);
            docOrder.insert(d.id, i++);
        }
    }
    QString text(const Segment &s)
    {
        if (s.kind != SegmentKind::Text)
            return Summaries::segmentText(project, s);
        if (!texts.contains(s.documentId))
            texts.insert(s.documentId, project.documentText(s.documentId));
        return texts.value(s.documentId).mid(s.start, s.length());
    }
    // "Intervista 1 · 120–180 · peso 2"
    QString details(const Segment &s) const
    {
        QStringList parts{docs.value(s.documentId).name};
        if (s.kind == SegmentKind::Text)
            parts << QStringLiteral("%1–%2").arg(s.start).arg(s.end);
        if (s.weight)
            parts << QObject::tr("peso %1").arg(s.weight);
        return parts.join(QStringLiteral(" · "));
    }
    void sort(QList<Segment> *list) const
    {
        std::stable_sort(list->begin(), list->end(), [this](const Segment &a, const Segment &b) {
            const int oa = docOrder.value(a.documentId), ob = docOrder.value(b.documentId);
            if (oa != ob)
                return oa < ob;
            return a.kind == SegmentKind::Text && b.kind == SegmentKind::Text ? a.start < b.start : a.id < b.id;
        });
    }
};

void addCodebook(const Project &project, const Options &opt, SegmentSource &src, const QList<Segment> &segments,
                 Document *out)
{
    const QList<Code> codes = project.codes();
    const QHash<qint64, int> counts = project.segmentCountByCode();
    const QHash<qint64, qint64> memoIds = project.memoIdsFor(MemoTarget::Code);
    QHash<qint64, QList<Code>> children;
    for (const Code &c : codes)
        children[c.parentId] << c;
    QHash<qint64, QList<Segment>> byCode;
    if (opt.examplesPerCode > 0)
        for (const Segment &s : segments)
            byCode[s.codeId] << s;

    out->blocks << heading(QObject::tr("Codebook"), 1);
    if (codes.isEmpty())
        out->blocks << block(BlockType::Paragraph, {run(QObject::tr("Il progetto non ha codici."), false, true)});
    std::function<void(qint64, int)> walk = [&](qint64 parent, int depth) {
        for (const Code &c : children.value(parent)) {
            Block title = block(BlockType::Paragraph,
                                {run(QStringLiteral("■ "), false, false, c.color), run(c.name, true),
                                 run(QStringLiteral("  (%1)").arg(QObject::tr("%n segmenti", nullptr, counts.value(c.id))),
                                     false, false, QColor(Qt::darkGray))},
                                depth);
            out->blocks << title;
            if (!c.description.trimmed().isEmpty())
                out->blocks << block(BlockType::Paragraph, {run(c.description.trimmed())}, depth);
            if (memoIds.contains(c.id)) {
                const QString body = project.memo(memoIds.value(c.id)).body.trimmed();
                if (!body.isEmpty())
                    out->blocks << block(BlockType::Paragraph, {run(QObject::tr("Memo: "), true, true), run(body, false, true)},
                                         depth);
            }
            QList<Segment> examples = byCode.value(c.id);
            src.sort(&examples);
            for (int i = 0; i < examples.size() && i < opt.examplesPerCode; ++i) {
                Block q = block(BlockType::Quote, {run(cut(src.text(examples[i]), kExampleMaxChars))}, depth);
                q.accent = c.color;
                out->blocks << q;
                out->blocks << block(BlockType::Meta, {run(src.details(examples[i]))}, depth);
            }
            walk(c.id, depth + 1);
        }
    };
    walk(0, 0);
}

void addSegment(const Options &opt, SegmentSource &src, const Segment &s, const QColor &color, const QString &codeLabel,
                Document *out)
{
    Block q = block(BlockType::Quote, {run(src.text(s))});
    q.accent = color;
    out->blocks << q;
    QString meta = src.details(s);
    if (!codeLabel.isEmpty())
        meta = codeLabel + QStringLiteral(" · ") + meta;
    out->blocks << block(BlockType::Meta, {run(meta)});
    if (opt.comments && !s.comment.trimmed().isEmpty())
        out->blocks << block(BlockType::Paragraph, {run(QObject::tr("Commento: "), true), run(s.comment.trimmed())});
}

void addSegments(const Project &project, const Options &opt, SegmentSource &src, const QList<Segment> &segments,
                 Document *out)
{
    out->blocks << heading(QObject::tr("Segmenti codificati"), 1);
    if (segments.isEmpty()) {
        out->blocks << block(BlockType::Paragraph, {run(QObject::tr("Nessun segmento codificato."), false, true)});
        return;
    }
    QHash<qint64, Code> codes;
    for (const Code &c : project.codes())
        codes.insert(c.id, c);

    if (opt.grouping == Grouping::ByCode) {
        QHash<qint64, QList<Segment>> byCode;
        for (const Segment &s : segments)
            byCode[s.codeId] << s;
        QHash<qint64, QList<Code>> children;
        for (const Code &c : project.codes())
            children[c.parentId] << c;
        std::function<void(qint64)> walk = [&](qint64 parent) {
            for (const Code &c : children.value(parent)) {
                QList<Segment> list = byCode.value(c.id);
                if (!list.isEmpty()) {
                    src.sort(&list);
                    Block h = heading(project.codePath(c.id), 2);
                    out->blocks << h;
                    out->blocks << block(BlockType::Meta, {run(QObject::tr("%n segmenti", nullptr, int(list.size())))});
                    for (const Segment &s : list)
                        addSegment(opt, src, s, c.color, QString(), out);
                }
                walk(c.id);
            }
        };
        walk(0);
    } else {
        QHash<qint64, QList<Segment>> byDoc;
        for (const Segment &s : segments)
            byDoc[s.documentId] << s;
        QList<qint64> order = byDoc.keys();
        std::sort(order.begin(), order.end(),
                  [&](qint64 a, qint64 b) { return src.docOrder.value(a) < src.docOrder.value(b); });
        for (qint64 docId : order) {
            QList<Segment> list = byDoc.value(docId);
            src.sort(&list);
            out->blocks << heading(src.docs.value(docId).name, 2);
            out->blocks << block(BlockType::Meta, {run(QObject::tr("%n segmenti", nullptr, int(list.size())))});
            for (const Segment &s : list)
                addSegment(opt, src, s, codes.value(s.codeId).color, project.codePath(s.codeId), out);
        }
    }
}

void addMemos(const Project &project, Document *out)
{
    out->blocks << heading(QObject::tr("Memo"), 1);
    const QList<Memo> memos = project.memos();
    if (memos.isEmpty())
        out->blocks << block(BlockType::Paragraph, {run(QObject::tr("Il progetto non ha memo."), false, true)});
    for (const Memo &m : memos) {
        out->blocks << heading(m.title.trimmed().isEmpty() ? QObject::tr("Memo senza titolo") : m.title.trimmed(), 2);
        QStringList meta{Exporters::memoLinksLabel(project, m)};
        if (!m.author.isEmpty())
            meta << m.author;
        if (m.modifiedAt.isValid())
            meta << dateLabel(m.modifiedAt);
        out->blocks << block(BlockType::Meta, {run(meta.join(QStringLiteral(" · ")))});
        for (const QString &line : m.body.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty())
                out->blocks << block(BlockType::Paragraph, {run(line)});
    }
}

void addTable(const QString &title, const Exporters::Table &rows, Document *out)
{
    out->blocks << heading(title, 2);
    Block t;
    t.type = BlockType::Table;
    t.rows = rows;
    out->blocks << t;
}

} // namespace

Document build(const Project &project, const Options &options)
{
    Document doc;
    doc.title = options.title.trimmed().isEmpty() ? project.name() : options.title.trimmed();
    doc.blocks << block(BlockType::Title, {run(doc.title)});
    doc.blocks << block(BlockType::Subtitle,
                        {run(QObject::tr("Progetto %1 – %2").arg(project.name(), QLocale().toString(QDate::currentDate(), QLocale::LongFormat)))});
    if (options.contents)
        doc.blocks << block(BlockType::Contents, {run(QObject::tr("Indice"))});

    SegmentSource src(project);
    QList<Segment> segments;
    if (options.segments || (options.codebook && options.examplesPerCode > 0))
        segments = project.allSegments(true);

    if (options.codebook)
        addCodebook(project, options, src, segments, &doc);
    if (options.segments)
        addSegments(project, options, src, segments, &doc);
    if (options.memos)
        addMemos(project, &doc);
    if (options.variables) {
        doc.blocks << heading(QObject::tr("Variabili dei documenti"), 1);
        addTable(QObject::tr("Valori delle variabili"), Exporters::variablesTable(project), &doc);
    }
    if (options.summaryTables) {
        doc.blocks << heading(QObject::tr("Tabelle riassuntive"), 1);
        const QList<SummaryTable> tables = project.summaryTables();
        if (tables.isEmpty())
            doc.blocks << block(BlockType::Paragraph, {run(QObject::tr("Il progetto non ha tabelle riassuntive."), false, true)});
        for (const SummaryTable &t : tables)
            addTable(t.name, Summaries::buildTable(project, Summaries::configFromJson(t.configJson)), &doc);
    }
    return doc;
}

// ---------------------------------------------------------------- HTML / PDF

namespace {

QString htmlText(const QString &text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
}

QString htmlRuns(const QList<Run> &runs)
{
    QString html;
    for (const Run &r : runs) {
        QString t = htmlText(r.text);
        if (r.color.isValid())
            t = QStringLiteral("<span style=\"color:%1\">%2</span>").arg(r.color.name(), t);
        if (r.bold)
            t = QStringLiteral("<b>%1</b>").arg(t);
        if (r.italic)
            t = QStringLiteral("<i>%1</i>").arg(t);
        html += t;
    }
    return html;
}

const char *kReportCss =
    "body{font-family:'Segoe UI',Calibri,Arial,sans-serif;font-size:10pt;color:#222}"
    "h1{font-size:18pt;color:#1f3864;margin-top:18px}h2{font-size:14pt;color:#2f5496}h3{font-size:12pt;color:#2f5496}"
    ".title{font-size:26pt;font-weight:bold;color:#1f3864}.subtitle{font-size:12pt;color:#666}"
    ".meta{font-size:8.5pt;color:#777}.quote{font-style:italic}"
    "table{border-collapse:collapse}th{background:#d9e2f3}th,td{border:1px solid #999;padding:4px;vertical-align:top}";

} // namespace

QString toHtml(const Document &document)
{
    QString toc;
    QString body;
    int counter = 0;
    for (const Block &b : document.blocks) {
        const QString runs = htmlRuns(b.runs);
        const QString indent = b.level > 0 ? QString::number(b.level * 18) : QStringLiteral("0");
        switch (b.type) {
        case BlockType::Title:
            body += QStringLiteral("<p class=\"title\">%1</p>").arg(runs);
            break;
        case BlockType::Subtitle:
            body += QStringLiteral("<p class=\"subtitle\">%1</p>").arg(runs);
            break;
        case BlockType::Contents:
            body += QStringLiteral("<!--TOC-->");
            break;
        case BlockType::Heading: {
            const int level = qBound(1, b.level, 3);
            ++counter;
            toc += QStringLiteral("<p style=\"margin:2px 0 2px %1px\"><a href=\"#s%2\">%3</a></p>")
                       .arg((level - 1) * 18)
                       .arg(counter)
                       .arg(runs);
            body += QStringLiteral("<h%1%2><a name=\"s%3\"></a>%4</h%1>")
                        .arg(level)
                        .arg(b.pageBreakBefore ? QStringLiteral(" style=\"page-break-before:always\"") : QString())
                        .arg(counter)
                        .arg(runs);
            break;
        }
        case BlockType::Paragraph:
            body += QStringLiteral("<p style=\"margin-left:%1px\">%2</p>").arg(indent, runs);
            break;
        case BlockType::Meta:
            body += QStringLiteral("<p class=\"meta\" style=\"margin-left:%1px;margin-top:0\">%2</p>").arg(indent, runs);
            break;
        case BlockType::Quote: {
            const QString color = b.accent.isValid() ? b.accent.name() : QStringLiteral("#999999");
            body += QStringLiteral("<p class=\"quote\" style=\"margin-left:%1px;margin-bottom:2px;"
                                   "border-left:3px solid %2;padding-left:8px\">%3</p>")
                        .arg(QString::number(b.level * 18 + 8), color, runs);
            break;
        }
        case BlockType::Table: {
            body += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\" width=\"100%\">");
            for (qsizetype r = 0; r < b.rows.size(); ++r) {
                body += QStringLiteral("<tr>");
                for (const QString &cell : b.rows[r])
                    body += (r == 0 ? QStringLiteral("<th>") : QStringLiteral("<td>")) + htmlText(cell) +
                            (r == 0 ? QStringLiteral("</th>") : QStringLiteral("</td>"));
                body += QStringLiteral("</tr>");
            }
            body += QStringLiteral("</table>");
            break;
        }
        }
    }
    body.replace(QStringLiteral("<!--TOC-->"),
                 toc.isEmpty() ? QString()
                               : QStringLiteral("<h2>%1</h2>%2").arg(htmlText(QObject::tr("Indice")), toc));
    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title><style>%2</style></head>"
                          "<body>%3</body></html>")
        .arg(document.title.toHtmlEscaped(), QString::fromLatin1(kReportCss), body);
}

bool writeHtml(const QString &path, const Document &document, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toHtml(document).toUtf8()) < 0 || !file.commit()) {
        *error = QObject::tr("Impossibile scrivere %1").arg(path);
        return false;
    }
    return true;
}

bool writePdf(const QString &path, const Document &document, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("Impossibile scrivere %1").arg(path);
        return false;
    }
    {
        QPdfWriter writer(&file);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);
        writer.setTitle(document.title);
        writer.setCreator(QStringLiteral("Tessera"));
        QTextDocument text;
        text.setHtml(toHtml(document));
        text.print(&writer);
    }
    file.close();
    if (file.size() == 0) {
        *error = QObject::tr("Impossibile scrivere %1").arg(path);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- DOCX

namespace {

const char kXmlHeader[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
const char kWNs[] = "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"";

bool xmlAllowed(char32_t c)
{
    return c == 0x9 || c == 0xA || c == 0xD || (c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD) ||
           (c >= 0x10000 && c <= 0x10FFFF);
}

QString xmlText(const QString &s)
{
    QString out;
    out.reserve(s.size());
    const QList<uint> points = s.toUcs4();
    for (char32_t c : points) {
        if (!xmlAllowed(c))
            continue;
        switch (c) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        default: out += QString::fromUcs4(&c, 1);
        }
    }
    return out;
}

QString hex(const QColor &c) { return c.name().mid(1).toUpper(); }

// Runs of a paragraph; `size` in half-points (0 = the style's), `gray` forces the grey of the details.
QString docxRuns(const QList<Run> &runs, int size = 0)
{
    QString xml;
    for (const Run &r : runs) {
        QString props;
        if (r.bold)
            props += QStringLiteral("<w:b/>");
        if (r.italic)
            props += QStringLiteral("<w:i/>");
        if (r.color.isValid())
            props += QStringLiteral("<w:color w:val=\"%1\"/>").arg(hex(r.color));
        if (size)
            props += QStringLiteral("<w:sz w:val=\"%1\"/>").arg(size);
        const QString rPr = props.isEmpty() ? QString() : QStringLiteral("<w:rPr>%1</w:rPr>").arg(props);
        const QStringList lines = r.text.split(QLatin1Char('\n'));
        xml += QStringLiteral("<w:r>") + rPr;
        for (qsizetype i = 0; i < lines.size(); ++i) {
            if (i > 0)
                xml += QStringLiteral("<w:br/>");
            xml += QStringLiteral("<w:t xml:space=\"preserve\">") + xmlText(lines[i]) + QStringLiteral("</w:t>");
        }
        xml += QStringLiteral("</w:r>");
    }
    return xml;
}

QString docxParagraph(const QString &style, const QString &extraPPr, const QString &content)
{
    return QStringLiteral("<w:p><w:pPr><w:pStyle w:val=\"%1\"/>%2</w:pPr>%3</w:p>").arg(style, extraPPr, content);
}

QString docxTable(const Exporters::Table &rows)
{
    const int columns = rows.isEmpty() ? 1 : int(std::max_element(rows.begin(), rows.end(), [](const QStringList &a, const QStringList &b) {
                                                       return a.size() < b.size();
                                                   })->size());
    const int width = 9638 / std::max(1, columns); // A4 minus 2 cm margins, in twentieths of a point
    QString xml = QStringLiteral("<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/><w:tblW w:w=\"5000\" w:type=\"pct\"/>"
                                 "<w:tblLayout w:type=\"fixed\"/><w:tblLook w:val=\"04A0\" w:firstRow=\"1\" w:lastRow=\"0\" "
                                 "w:firstColumn=\"0\" w:lastColumn=\"0\" w:noHBand=\"0\" w:noVBand=\"1\"/></w:tblPr><w:tblGrid>");
    for (int c = 0; c < columns; ++c)
        xml += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(width);
    xml += QStringLiteral("</w:tblGrid>");
    for (qsizetype r = 0; r < rows.size(); ++r) {
        xml += QStringLiteral("<w:tr>");
        if (r == 0)
            xml += QStringLiteral("<w:trPr><w:cantSplit/><w:tblHeader/></w:trPr>");
        for (int c = 0; c < columns; ++c) {
            const QString text = c < rows[r].size() ? rows[r][c] : QString();
            xml += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/>%2</w:tcPr>").arg(width).arg(
                r == 0 ? QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"D9E2F3\"/>") : QString());
            xml += docxParagraph(QStringLiteral("TableText"), QString(), docxRuns({run(text, r == 0)}));
            xml += QStringLiteral("</w:tc>");
        }
        xml += QStringLiteral("</w:tr>");
    }
    xml += QStringLiteral("</w:tbl>");
    // Word needs a paragraph after a table.
    xml += docxParagraph(QStringLiteral("Normal"), QString(), QString());
    return xml;
}

QString documentXml(const Document &doc)
{
    QString body;
    for (const Block &b : doc.blocks) {
        const QString pageBreak = b.pageBreakBefore ? QStringLiteral("<w:pageBreakBefore/>") : QString();
        const QString indent = b.level > 0 && (b.type == BlockType::Paragraph || b.type == BlockType::Meta)
                                   ? QStringLiteral("<w:ind w:left=\"%1\"/>").arg(b.level * 360)
                                   : QString();
        switch (b.type) {
        case BlockType::Title:
            body += docxParagraph(QStringLiteral("Title"), QString(), docxRuns(b.runs));
            break;
        case BlockType::Subtitle:
            body += docxParagraph(QStringLiteral("Subtitle"), QString(), docxRuns(b.runs));
            break;
        case BlockType::Contents:
            body += docxParagraph(QStringLiteral("TOCHeading"), QString(), docxRuns(b.runs));
            body += QStringLiteral("<w:p><w:r><w:fldChar w:fldCharType=\"begin\" w:dirty=\"true\"/></w:r>"
                                   "<w:r><w:instrText xml:space=\"preserve\"> TOC \\o \"1-3\" \\h \\z \\u </w:instrText></w:r>"
                                   "<w:r><w:fldChar w:fldCharType=\"separate\"/></w:r>"
                                   "<w:r><w:t>%1</w:t></w:r><w:r><w:fldChar w:fldCharType=\"end\"/></w:r></w:p>")
                        .arg(xmlText(QObject::tr("Per generare l'indice: clic destro e Aggiorna campo.")));
            break;
        case BlockType::Heading:
            body += docxParagraph(QStringLiteral("Heading%1").arg(qBound(1, b.level, 3)), pageBreak, docxRuns(b.runs));
            break;
        case BlockType::Paragraph:
            body += docxParagraph(QStringLiteral("Normal"), indent, docxRuns(b.runs));
            break;
        case BlockType::Meta:
            body += docxParagraph(QStringLiteral("Meta"), indent, docxRuns(b.runs));
            break;
        case BlockType::Quote: {
            const QString border = QStringLiteral("<w:pBdr><w:left w:val=\"single\" w:sz=\"24\" w:space=\"8\" w:color=\"%1\"/></w:pBdr>")
                                       .arg(b.accent.isValid() ? hex(b.accent) : QStringLiteral("999999"));
            body += docxParagraph(QStringLiteral("Quote"), border + QStringLiteral("<w:ind w:left=\"%1\"/>").arg(360 + b.level * 360),
                                  docxRuns(b.runs));
            break;
        }
        case BlockType::Table:
            body += docxTable(b.rows);
            break;
        }
    }
    return QString::fromLatin1(kXmlHeader) + QStringLiteral("<w:document %1><w:body>%2").arg(QLatin1String(kWNs), body) +
           QStringLiteral("<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1134\" w:right=\"1134\" "
                          "w:bottom=\"1134\" w:left=\"1134\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr>"
                          "</w:body></w:document>");
}

QString paragraphStyle(const QString &id, const QString &name, const QString &pPr, const QString &rPr,
                       const QString &basedOn = QStringLiteral("Normal"), bool qformat = true)
{
    return QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"%1\"><w:name w:val=\"%2\"/><w:basedOn w:val=\"%3\"/>"
                          "<w:next w:val=\"Normal\"/>%4<w:pPr>%5</w:pPr><w:rPr>%6</w:rPr></w:style>")
        .arg(id, name, basedOn, qformat ? QStringLiteral("<w:qFormat/>") : QString(), pPr, rPr);
}

QString stylesXml()
{
    QString xml = QString::fromLatin1(kXmlHeader) + QStringLiteral("<w:styles %1>").arg(QLatin1String(kWNs));
    xml += QStringLiteral("<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" "
                          "w:eastAsia=\"Calibri\" w:cs=\"Calibri\"/><w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/>"
                          "<w:lang w:val=\"it-IT\" w:eastAsia=\"it-IT\" w:bidi=\"ar-SA\"/></w:rPr></w:rPrDefault>"
                          "<w:pPrDefault><w:pPr><w:spacing w:after=\"120\" w:line=\"264\" w:lineRule=\"auto\"/></w:pPr>"
                          "</w:pPrDefault></w:docDefaults>");
    xml += QStringLiteral("<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/>"
                          "<w:qFormat/></w:style>");
    xml += QStringLiteral("<w:style w:type=\"character\" w:default=\"1\" w:styleId=\"DefaultParagraphFont\">"
                          "<w:name w:val=\"Default Paragraph Font\"/><w:uiPriority w:val=\"1\"/><w:semiHidden/></w:style>");
    xml += paragraphStyle(QStringLiteral("Title"), QStringLiteral("Title"),
                          QStringLiteral("<w:spacing w:before=\"2400\" w:after=\"240\"/>"),
                          QStringLiteral("<w:b/><w:color w:val=\"1F3864\"/><w:sz w:val=\"56\"/><w:szCs w:val=\"56\"/>"));
    xml += paragraphStyle(QStringLiteral("Subtitle"), QStringLiteral("Subtitle"), QStringLiteral("<w:spacing w:after=\"480\"/>"),
                          QStringLiteral("<w:color w:val=\"666666\"/><w:sz w:val=\"26\"/><w:szCs w:val=\"26\"/>"));
    const char *colors[] = {"1F3864", "2F5496", "2F5496"};
    const int sizes[] = {36, 28, 24};
    for (int i = 1; i <= 3; ++i)
        xml += paragraphStyle(QStringLiteral("Heading%1").arg(i), QStringLiteral("heading %1").arg(i),
                              QStringLiteral("<w:keepNext/><w:keepLines/><w:spacing w:before=\"%1\" w:after=\"120\"/>"
                                             "<w:outlineLvl w:val=\"%2\"/>")
                                  .arg(i == 1 ? 360 : 240)
                                  .arg(i - 1),
                              QStringLiteral("<w:b/><w:color w:val=\"%1\"/><w:sz w:val=\"%2\"/><w:szCs w:val=\"%2\"/>")
                                  .arg(QLatin1String(colors[i - 1]))
                                  .arg(sizes[i - 1]));
    xml += paragraphStyle(QStringLiteral("Quote"), QStringLiteral("Quote"),
                          QStringLiteral("<w:spacing w:before=\"120\" w:after=\"40\"/>"), QStringLiteral("<w:i/>"));
    xml += paragraphStyle(QStringLiteral("Meta"), QStringLiteral("Meta"), QStringLiteral("<w:spacing w:after=\"160\"/>"),
                          QStringLiteral("<w:color w:val=\"777777\"/><w:sz w:val=\"17\"/><w:szCs w:val=\"17\"/>"));
    xml += paragraphStyle(QStringLiteral("TableText"), QStringLiteral("Table Text"),
                          QStringLiteral("<w:spacing w:after=\"40\" w:line=\"240\" w:lineRule=\"auto\"/>"),
                          QStringLiteral("<w:sz w:val=\"18\"/><w:szCs w:val=\"18\"/>"));
    xml += paragraphStyle(QStringLiteral("TOCHeading"), QStringLiteral("TOC Heading"),
                          QStringLiteral("<w:keepNext/><w:spacing w:before=\"480\" w:after=\"120\"/>"),
                          QStringLiteral("<w:b/><w:color w:val=\"2F5496\"/><w:sz w:val=\"28\"/><w:szCs w:val=\"28\"/>"));
    for (int i = 1; i <= 3; ++i)
        xml += paragraphStyle(QStringLiteral("TOC%1").arg(i), QStringLiteral("toc %1").arg(i),
                              QStringLiteral("<w:spacing w:after=\"60\"/><w:ind w:left=\"%1\"/>").arg((i - 1) * 220),
                              QString(), QStringLiteral("Normal"), false);
    xml += QStringLiteral("<w:style w:type=\"table\" w:default=\"1\" w:styleId=\"TableNormal\"><w:name w:val=\"Normal Table\"/>"
                          "<w:uiPriority w:val=\"99\"/><w:semiHidden/><w:tblPr><w:tblInd w:w=\"0\" w:type=\"dxa\"/>"
                          "<w:tblCellMar><w:top w:w=\"0\" w:type=\"dxa\"/><w:left w:w=\"108\" w:type=\"dxa\"/>"
                          "<w:bottom w:w=\"0\" w:type=\"dxa\"/><w:right w:w=\"108\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr></w:style>");
    xml += QStringLiteral("<w:style w:type=\"table\" w:styleId=\"TableGrid\"><w:name w:val=\"Table Grid\"/>"
                          "<w:basedOn w:val=\"TableNormal\"/><w:uiPriority w:val=\"39\"/><w:tblPr><w:tblBorders>"
                          "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"999999\"/>"
                          "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"999999\"/>"
                          "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"999999\"/>"
                          "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"999999\"/>"
                          "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"999999\"/>"
                          "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"999999\"/>"
                          "</w:tblBorders></w:tblPr></w:style>");
    xml += QStringLiteral("</w:styles>");
    return xml;
}

} // namespace

bool writeDocx(const QString &path, const Document &document, QString *error)
{
    const QString header = QString::fromLatin1(kXmlHeader);
    const QString types = header +
        QStringLiteral("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                       "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                       "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                       "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
                       "wordprocessingml.document.main+xml\"/>"
                       "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument."
                       "wordprocessingml.styles+xml\"/>"
                       "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package."
                       "core-properties+xml\"/></Types>");
    const QString rootRels = header +
        QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                       "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                       "officeDocument\" Target=\"word/document.xml\"/>"
                       "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/"
                       "core-properties\" Target=\"docProps/core.xml\"/></Relationships>");
    const QString docRels = header +
        QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                       "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                       "styles\" Target=\"styles.xml\"/></Relationships>");
    const QString core = header +
        QStringLiteral("<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                       "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                       "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:title>%1</dc:title>"
                       "<dc:creator>Tessera</dc:creator><dcterms:created xsi:type=\"dcterms:W3CDTF\">%2</dcterms:created>"
                       "</cp:coreProperties>")
            .arg(xmlText(document.title), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    // Built in memory and saved with QSaveFile, as in the Excel export: QZipWriter::close() closes its
    // device, which QSaveFile forbids.
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QZipWriter zip(&buffer);
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    zip.addFile(QStringLiteral("[Content_Types].xml"), types.toUtf8());
    zip.addFile(QStringLiteral("_rels/.rels"), rootRels.toUtf8());
    zip.addFile(QStringLiteral("docProps/core.xml"), core.toUtf8());
    zip.addFile(QStringLiteral("word/document.xml"), documentXml(document).toUtf8());
    zip.addFile(QStringLiteral("word/styles.xml"), stylesXml().toUtf8());
    zip.addFile(QStringLiteral("word/_rels/document.xml.rels"), docRels.toUtf8());
    zip.close();
    QSaveFile file(path);
    if (zip.status() != QZipWriter::NoError || !file.open(QIODevice::WriteOnly)
        || file.write(buffer.data()) != buffer.data().size() || !file.commit()) {
        *error = QObject::tr("Impossibile scrivere %1").arg(path);
        return false;
    }
    return true;
}

} // namespace Report
