#include "core/Importers.h"
#include "core/Images.h"
#include "core/Media.h"
#include "core/RichText.h"
#include "core/TextUtils.h"
#include "core/Transcripts.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <cctype>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QStringDecoder>
#include <QTextDocument>
#include <QXmlStreamReader>
#include <QtCore/private/qzipreader_p.h>

namespace Importers {

namespace {

const QString kOdfTextNamespace = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");

Result failure(const QString &message) { return {false, {}, message}; }

Result success(const QString &text)
{
    Result r;
    r.ok = true;
    r.text = TextUtils::normalizeText(text);
    // Collapse trailing whitespace lines left by converters.
    while (r.text.endsWith(QLatin1Char('\n')) || r.text.endsWith(QLatin1Char(' ')))
        r.text.chop(1);
    return r;
}

// Text and formatting collected by a Builder. The formatting is kept only if the text did not change
// in normalization (it would shift the ranges) and is cut at the trimmed end.
Result successRich(const RichText::Builder &b)
{
    Result r = success(b.text);
    if (r.text != b.text.left(r.text.size()))
        return r;
    RichText::Formatting f = b.formatting();
    RichText::clamp(f, int(r.text.size()));
    r.richContent = RichText::toJson(f);
    return r;
}

bool readFile(const QString &path, QByteArray *data, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Impossibile aprire %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return false;
    }
    *data = f.readAll();
    return true;
}

QByteArray zipEntry(const QString &path, const QString &entry, QString *error)
{
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        *error = QObject::tr("%1 non è un archivio valido").arg(QFileInfo(path).fileName());
        return {};
    }
    const QByteArray data = zip.fileData(entry);
    if (data.isEmpty())
        *error = QObject::tr("%1 non contiene %2").arg(QFileInfo(path).fileName(), entry);
    return data;
}

} // namespace

QStringList supportedSuffixes()
{
    return {QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("docx"), QStringLiteral("odt"),
            QStringLiteral("rtf"), QStringLiteral("pdf"), QStringLiteral("html"), QStringLiteral("htm"),
            QStringLiteral("srt"), QStringLiteral("vtt")};
}

QString fileDialogFilter()
{
    QStringList patterns;
    for (const QString &s : supportedSuffixes())
        patterns << QStringLiteral("*.") + s;
    QStringList imagePatterns, mediaPatterns;
    for (const QString &s : Media::audioSuffixes() + Media::videoSuffixes()) {
        patterns << QStringLiteral("*.") + s;
        mediaPatterns << QStringLiteral("*.") + s;
    }
    for (const QString &s : Images::suffixes()) {
        patterns << QStringLiteral("*.") + s;
        imagePatterns << QStringLiteral("*.") + s;
    }
    return QObject::tr("Documenti supportati (%1);;Testo (*.txt *.md);;Word (*.docx);;OpenDocument (*.odt);;"
                       "RTF (*.rtf);;PDF (*.pdf);;HTML (*.html *.htm);;Trascrizioni con tempi (*.srt *.vtt);;Immagini (%2);;Audio e video (%3);;Tutti i file (*)")
        .arg(patterns.join(QLatin1Char(' ')), imagePatterns.join(QLatin1Char(' ')), mediaPatterns.join(QLatin1Char(' ')));
}

QString decodeText(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(data.mid(3));
    if (data.startsWith("\xFF\xFE"))
        return QStringDecoder(QStringDecoder::Utf16LE).decode(data.mid(2));
    if (data.startsWith("\xFE\xFF"))
        return QStringDecoder(QStringDecoder::Utf16BE).decode(data.mid(2));
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(data);
    if (!utf8.hasError())
        return text;
    QStringDecoder legacy(QStringDecoder::System);
    return legacy.decode(data);
}

Result importFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("docx"))
        return importDocx(path);
    if (suffix == QLatin1String("odt"))
        return importOdt(path);
    if (suffix == QLatin1String("pdf"))
        return importPdf(path);
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        return importHtml(path);
    if (suffix == QLatin1String("rtf"))
        return importRtf(path);
    if (suffix == QLatin1String("doc"))
        return failure(QObject::tr("Il formato .doc (Word 97-2003) non è supportato: salvalo come .docx"));
    QByteArray data;
    QString error;
    if (!readFile(path, &data, &error))
        return failure(error);
    const QString decoded = decodeText(data);
    if (suffix == QLatin1String("srt") || suffix == QLatin1String("vtt") || suffix == QLatin1String("txt")) {
        const Transcripts::Parsed parsed = Transcripts::parseByName(path, decoded);
        if (parsed.ok) {
            Result r = success(parsed.text);
            r.stamps = parsed.stamps;
            return r;
        }
        if (suffix != QLatin1String("txt"))
            return failure(QObject::tr("%1 non contiene sottotitoli con tempi").arg(QFileInfo(path).fileName()));
    }
    return success(decoded);
}

const QString kWordNamespace = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");

struct DocxStyle {
    int headingLevel = 0;
    RichText::BlockKind list = RichText::BlockKind::Paragraph;
};

// styleId -> heading level / list kind, from the style names Word uses ("heading 2", "List Bullet").
QHash<QString, DocxStyle> readDocxStyles(const QByteArray &xml)
{
    QHash<QString, DocxStyle> styles;
    static const QRegularExpression heading(QStringLiteral("^(?:heading|titolo)\\s*(\\d)$"),
                                            QRegularExpression::CaseInsensitiveOption);
    QXmlStreamReader r(xml);
    QString id;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        if (r.name() == QLatin1String("style")) {
            id = r.attributes().value(kWordNamespace, QLatin1String("styleId")).toString();
        } else if (r.name() == QLatin1String("name") && !id.isEmpty()) {
            const QString name = r.attributes().value(kWordNamespace, QLatin1String("val")).toString().trimmed();
            DocxStyle s;
            const QRegularExpressionMatch m = heading.match(name);
            if (m.hasMatch())
                s.headingLevel = qBound(1, m.captured(1).toInt(), 6);
            else if (name.compare(QLatin1String("title"), Qt::CaseInsensitive) == 0)
                s.headingLevel = 1;
            else if (name.startsWith(QLatin1String("list bullet"), Qt::CaseInsensitive))
                s.list = RichText::BlockKind::Bullet;
            else if (name.startsWith(QLatin1String("list number"), Qt::CaseInsensitive))
                s.list = RichText::BlockKind::Numbered;
            if (s.headingLevel || s.list != RichText::BlockKind::Paragraph)
                styles.insert(id, s);
        }
    }
    return styles;
}

// numId -> (ilvl -> is a bullet list), from numbering.xml.
QHash<int, QHash<int, bool>> readDocxNumbering(const QByteArray &xml)
{
    QHash<int, QHash<int, bool>> abstracts, nums;
    QHash<int, int> numToAbstract;
    QXmlStreamReader r(xml);
    int abstractId = -1, level = 0, numId = -1;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        const auto val = [&] { return r.attributes().value(kWordNamespace, QLatin1String("val")).toString(); };
        const auto name = r.name();
        if (name == QLatin1String("abstractNum")) {
            abstractId = r.attributes().value(kWordNamespace, QLatin1String("abstractNumId")).toInt();
        } else if (name == QLatin1String("lvl")) {
            level = r.attributes().value(kWordNamespace, QLatin1String("ilvl")).toInt();
        } else if (name == QLatin1String("numFmt") && abstractId >= 0) {
            abstracts[abstractId].insert(level, val() == QLatin1String("bullet"));
        } else if (name == QLatin1String("num")) {
            numId = r.attributes().value(kWordNamespace, QLatin1String("numId")).toInt();
            abstractId = -1;
        } else if (name == QLatin1String("abstractNumId") && numId >= 0) {
            numToAbstract.insert(numId, val().toInt());
        }
    }
    for (auto it = numToAbstract.cbegin(); it != numToAbstract.cend(); ++it)
        nums.insert(it.key(), abstracts.value(it.value()));
    return nums;
}

Result importDocx(const QString &path)
{
    QString error;
    const QByteArray xml = zipEntry(path, QStringLiteral("word/document.xml"), &error);
    if (xml.isEmpty())
        return failure(error);
    QString ignored;
    const QHash<QString, DocxStyle> styles = readDocxStyles(zipEntry(path, QStringLiteral("word/styles.xml"), &ignored));
    const QHash<int, QHash<int, bool>> numbering =
        readDocxNumbering(zipEntry(path, QStringLiteral("word/numbering.xml"), &ignored));

    using RichText::BlockKind;
    QXmlStreamReader r(xml);
    RichText::Builder b;
    bool inBody = false, inPPr = false, inRPr = false, headerRow = false;
    int runStyle = 0, tableDepth = 0, rowIndex = 0;
    QString paragraphStyle;
    int numId = 0, ilvl = 0;
    // w:val="0" / "false" / "none" switch a property off.
    const auto enabled = [&] {
        const QStringView v = r.attributes().value(kWordNamespace, QLatin1String("val"));
        return !(v == QLatin1String("0") || v == QLatin1String("false") || v == QLatin1String("off")
                 || v == QLatin1String("none"));
    };
    const auto toggle = [&](int flag) {
        if (enabled())
            runStyle |= flag;
        else
            runStyle &= ~flag;
    };
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto name = r.name();
            if (name == QLatin1String("body"))
                inBody = true;
            else if (!inBody)
                continue;
            else if (name == QLatin1String("p")) {
                paragraphStyle.clear();
                numId = 0;
                ilvl = 0;
            } else if (name == QLatin1String("pPr"))
                inPPr = true;
            else if (name == QLatin1String("pStyle"))
                paragraphStyle = r.attributes().value(kWordNamespace, QLatin1String("val")).toString();
            else if (name == QLatin1String("numId") && inPPr)
                numId = r.attributes().value(kWordNamespace, QLatin1String("val")).toInt();
            else if (name == QLatin1String("ilvl") && inPPr)
                ilvl = r.attributes().value(kWordNamespace, QLatin1String("val")).toInt();
            else if (name == QLatin1String("r"))
                runStyle = 0;
            else if (name == QLatin1String("rPr"))
                inRPr = true;
            else if (inRPr && !inPPr && name == QLatin1String("b"))
                toggle(RichText::Bold);
            else if (inRPr && !inPPr && name == QLatin1String("i"))
                toggle(RichText::Italic);
            else if (inRPr && !inPPr && name == QLatin1String("u"))
                toggle(RichText::Underline);
            else if (inRPr && !inPPr && (name == QLatin1String("strike") || name == QLatin1String("dstrike")))
                toggle(RichText::Strike);
            else if (name == QLatin1String("tbl")) {
                if (tableDepth++ == 0)
                    rowIndex = 0;
            } else if (name == QLatin1String("tr"))
                headerRow = false;
            else if (name == QLatin1String("tblHeader"))
                headerRow = true;
            else if (name == QLatin1String("t")) {
                b.style = runStyle;
                b.add(r.readElementText());
            } else if (name == QLatin1String("tab") && !inPPr) { // inside w:pPr it is a tab stop
                b.style = 0;
                b.add(QStringLiteral("\t"));
            } else if (name == QLatin1String("br") || name == QLatin1String("cr")) {
                b.style = 0;
                b.add(QStringLiteral("\n"));
            } else if (name == QLatin1String("noBreakHyphen")) {
                b.style = runStyle;
                b.add(QString(QChar(0x2011)));
            } else if (name == QLatin1String("delText") || name == QLatin1String("instrText"))
                r.skipCurrentElement();
        } else if (r.isEndElement() && inBody) {
            const auto name = r.name();
            if (name == QLatin1String("pPr"))
                inPPr = false;
            else if (name == QLatin1String("rPr"))
                inRPr = false;
            else if (name == QLatin1String("tr"))
                ++rowIndex;
            else if (name == QLatin1String("tbl"))
                tableDepth = qMax(0, tableDepth - 1);
            else if (name == QLatin1String("p")) {
                const DocxStyle style = styles.value(paragraphStyle);
                BlockKind kind = BlockKind::Paragraph;
                int level = 0;
                if (style.headingLevel) {
                    kind = BlockKind::Heading;
                    level = style.headingLevel;
                } else if (tableDepth > 0) {
                    kind = BlockKind::TableCell;
                    level = headerRow ? 2 : rowIndex % 2;
                } else if (numId > 0) {
                    kind = numbering.value(numId).value(ilvl, true) ? BlockKind::Bullet : BlockKind::Numbered;
                    level = ilvl;
                } else if (style.list != BlockKind::Paragraph) {
                    kind = style.list;
                }
                b.endParagraph(kind, level);
            }
        }
    }
    if (r.hasError())
        return failure(QObject::tr("Errore nel leggere il DOCX: %1").arg(r.errorString()));
    return successRich(b);
}

// ODF automatic and named styles: character properties by style name, and the kind of each level of
// the list styles (true = bullet).
struct OdfStyles {
    QHash<QString, int> text;
    QHash<QString, QHash<int, bool>> listBullet;
};

void readOdfStyles(const QByteArray &xml, OdfStyles *out)
{
    QXmlStreamReader r(xml);
    QString style, list;
    const auto flag = [&](const char *attr) {
        const QStringView v = r.attributes().value(QLatin1String(attr));
        return !v.isEmpty() && v != QLatin1String("none");
    };
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto name = r.name();
            if (name == QLatin1String("style"))
                style = r.attributes().value(QLatin1String("style:name")).toString();
            else if (name == QLatin1String("list-style"))
                list = r.attributes().value(QLatin1String("style:name")).toString();
            else if (name == QLatin1String("text-properties") && !style.isEmpty()) {
                int s = 0;
                if (r.attributes().value(QLatin1String("fo:font-weight")) == QLatin1String("bold"))
                    s |= RichText::Bold;
                const QStringView fs = r.attributes().value(QLatin1String("fo:font-style"));
                if (fs == QLatin1String("italic") || fs == QLatin1String("oblique"))
                    s |= RichText::Italic;
                if (flag("style:text-underline-style"))
                    s |= RichText::Underline;
                if (flag("style:text-line-through-style"))
                    s |= RichText::Strike;
                if (s)
                    out->text.insert(style, s);
            } else if ((name == QLatin1String("list-level-style-bullet") || name == QLatin1String("list-level-style-number"))
                       && !list.isEmpty()) {
                out->listBullet[list].insert(r.attributes().value(QLatin1String("text:level")).toInt(),
                                             name == QLatin1String("list-level-style-bullet"));
            }
        } else if (r.isEndElement()) {
            if (r.name() == QLatin1String("style"))
                style.clear();
            else if (r.name() == QLatin1String("list-style"))
                list.clear();
        }
    }
}

Result importOdt(const QString &path)
{
    QString error;
    const QByteArray xml = zipEntry(path, QStringLiteral("content.xml"), &error);
    if (xml.isEmpty())
        return failure(error);
    OdfStyles styles;
    QString ignored;
    readOdfStyles(zipEntry(path, QStringLiteral("styles.xml"), &ignored), &styles);
    readOdfStyles(xml, &styles);

    using RichText::BlockKind;
    QXmlStreamReader r(xml);
    RichText::Builder b;
    int paragraphDepth = 0, headingLevel = 0, tableDepth = 0, rowIndex = 0, headerRows = 0;
    QList<int> spanStack;
    QStringList listStack;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto name = r.name();
            if (name == QLatin1String("p") || name == QLatin1String("h")) {
                if (++paragraphDepth == 1) {
                    headingLevel = name == QLatin1String("h")
                        ? qBound(1, r.attributes().value(QLatin1String("text:outline-level")).toInt(), 6) : 0;
                    b.style = styles.text.value(r.attributes().value(QLatin1String("text:style-name")).toString());
                    spanStack.clear();
                }
            } else if (name == QLatin1String("span") && paragraphDepth > 0) {
                spanStack.append(b.style);
                b.style |= styles.text.value(r.attributes().value(QLatin1String("text:style-name")).toString());
            } else if (name == QLatin1String("list")) {
                const QString s = r.attributes().value(QLatin1String("text:style-name")).toString();
                listStack.append(s.isEmpty() && !listStack.isEmpty() ? listStack.last() : s);
            } else if (name == QLatin1String("table")) {
                if (tableDepth++ == 0)
                    rowIndex = 0;
            } else if (name == QLatin1String("table-header-rows"))
                ++headerRows;
            else if (name == QLatin1String("s")) { // text:c spaces, bounded: c="2000000000" would exhaust memory
                const int count = r.attributes().value(kOdfTextNamespace, QLatin1String("c")).toInt();
                b.add(QString(qBound(1, count, 1000), QLatin1Char(' ')));
            }
            else if (name == QLatin1String("tab"))
                b.add(QStringLiteral("\t"));
            else if (name == QLatin1String("line-break"))
                b.add(QStringLiteral("\n"));
            else if (name == QLatin1String("note") || name == QLatin1String("annotation"))
                r.skipCurrentElement();
        } else if (r.isEndElement()) {
            const auto name = r.name();
            if ((name == QLatin1String("p") || name == QLatin1String("h")) && paragraphDepth > 0) {
                --paragraphDepth;
                if (paragraphDepth == 0) {
                    BlockKind kind = BlockKind::Paragraph;
                    int level = 0;
                    if (headingLevel) {
                        kind = BlockKind::Heading;
                        level = headingLevel;
                    } else if (tableDepth > 0) {
                        kind = BlockKind::TableCell;
                        level = headerRows > 0 ? 2 : rowIndex % 2;
                    } else if (!listStack.isEmpty()) {
                        const int depth = int(listStack.size());
                        kind = styles.listBullet.value(listStack.last()).value(depth, true) ? BlockKind::Bullet
                                                                                           : BlockKind::Numbered;
                        level = depth - 1;
                    }
                    b.endParagraph(kind, level);
                    b.style = 0;
                }
            } else if (name == QLatin1String("span") && !spanStack.isEmpty()) {
                b.style = spanStack.takeLast();
            } else if (name == QLatin1String("list") && !listStack.isEmpty()) {
                listStack.removeLast();
            } else if (name == QLatin1String("table-row")) {
                ++rowIndex;
            } else if (name == QLatin1String("table-header-rows")) {
                headerRows = qMax(0, headerRows - 1);
            } else if (name == QLatin1String("table")) {
                tableDepth = qMax(0, tableDepth - 1);
            }
        } else if (r.isCharacters() && paragraphDepth > 0) {
            b.add(r.text().toString());
        }
    }
    if (r.hasError())
        return failure(QObject::tr("Errore nel leggere l'ODT: %1").arg(r.errorString()));
    return successRich(b);
}
Result importPdf(const QString &path)
{
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return failure(QObject::tr("Impossibile leggere il PDF %1").arg(QFileInfo(path).fileName()));
    QStringList pages;
    for (int i = 0; i < pdf.pageCount(); ++i)
        pages << TextUtils::normalizeText(pdf.getAllText(i).text()).trimmed();
    QString text = pages.join(QStringLiteral("\n\n"));
    if (text.trimmed().isEmpty())
        return failure(QObject::tr("Il PDF non contiene testo selezionabile (forse è una scansione)"));
    return success(text);
}

Result importHtml(const QString &path)
{
    QByteArray data;
    QString error;
    if (!readFile(path, &data, &error))
        return failure(error);
    QTextDocument doc;
    doc.setHtml(decodeText(data));
    // Formatting is read from the parsed document; the text must be exactly what toPlainText() gives.
    QString plain;
    const RichText::Formatting f = RichText::fromDocument(doc, &plain);
    Result r = success(doc.toPlainText());
    if (!plain.isEmpty() && r.text == plain.left(r.text.size())) {
        RichText::Formatting cut = f;
        RichText::clamp(cut, int(r.text.size()));
        r.richContent = RichText::toJson(cut);
    }
    return r;
}

Result importRtf(const QString &path)
{
    QByteArray data;
    QString error;
    if (!readFile(path, &data, &error))
        return failure(error);
    if (!data.startsWith("{\\rtf"))
        return failure(QObject::tr("%1 non è un file RTF valido").arg(QFileInfo(path).fileName()));

    static const QSet<QByteArray> skipDestinations = {
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "header", "headerl", "headerr", "headerf",
        "footer", "footerl", "footerr", "footerf", "footnote", "object", "themedata", "colorschememapping",
        "latentstyles", "datastore", "xmlnstbl", "listtable", "listoverridetable", "rsidtbl", "generator",
        "mmathPr", "filetbl", "revtbl", "pgdsctbl", "fldinst", "shp", "nonshppict", "bkmkstart", "bkmkend",
    };
    struct State {
        bool skip = false;
        int uc = 1;
        int style = 0;
    };
    QList<State> stack{State{}};
    RichText::Builder b;
    bool inTable = false;
    int rowIndex = 0;
    QByteArray pendingBytes; // \'hh sequences decoded together (multi-byte code pages)
    int skipChars = 0;
    QStringDecoder legacy(QStringDecoder::System);

    auto flushBytes = [&] {
        if (!pendingBytes.isEmpty()) {
            if (!stack.last().skip) {
                b.style = stack.last().style;
                b.add(legacy.decode(pendingBytes));
            }
            pendingBytes.clear();
        }
    };
    auto emitText = [&](const QString &s) {
        flushBytes();
        if (!stack.last().skip) {
            b.style = stack.last().style;
            b.add(s);
        }
    };
    // End of a paragraph or table row; rows are shaded lines, banded by row.
    auto endParagraph = [&](bool row) {
        flushBytes();
        if (stack.last().skip)
            return;
        b.style = 0;
        if (inTable || row) {
            b.endParagraph(RichText::BlockKind::TableCell, rowIndex % 2);
            if (row)
                ++rowIndex;
        } else {
            b.endParagraph();
        }
    };

    const qsizetype n = data.size();
    for (qsizetype i = 0; i < n; ++i) {
        const char c = data.at(i);
        if (c == '{') {
            flushBytes();
            stack.append(stack.last());
            // "{\*\dest" marks an ignorable destination
            if (i + 2 < n && data.at(i + 1) == '\\' && data.at(i + 2) == '*')
                stack.last().skip = true;
        } else if (c == '}') {
            flushBytes();
            if (stack.size() > 1)
                stack.removeLast();
        } else if (c == '\\') {
            if (i + 1 >= n)
                break;
            const char nx = data.at(i + 1);
            if (nx == '\\' || nx == '{' || nx == '}') {
                ++i;
                if (skipChars > 0) { --skipChars; continue; }
                emitText(QString(QLatin1Char(nx)));
            } else if (nx == '\'') {
                if (i + 3 < n) {
                    bool ok = false;
                    const char byte = char(data.mid(i + 2, 2).toInt(&ok, 16));
                    i += 3;
                    if (skipChars > 0) { --skipChars; continue; }
                    if (ok)
                        pendingBytes.append(byte);
                }
            } else if (nx == '~') {
                ++i;
                emitText(QString(QChar(0x00A0)));
            } else if (nx == '_') {
                ++i;
                emitText(QString(QChar(0x2011)));
            } else if (nx == '-' || nx == '*') {
                ++i;
            } else if (nx == '\n' || nx == '\r') {
                ++i;
                emitText(QStringLiteral("\n"));
            } else if (std::isalpha(static_cast<unsigned char>(nx))) {
                qsizetype j = i + 1;
                while (j < n && std::isalpha(static_cast<unsigned char>(data.at(j))))
                    ++j;
                const QByteArray word = data.mid(i + 1, j - i - 1);
                qsizetype k = j;
                if (k < n && (data.at(k) == '-' || std::isdigit(static_cast<unsigned char>(data.at(k))))) {
                    ++k;
                    while (k < n && std::isdigit(static_cast<unsigned char>(data.at(k))))
                        ++k;
                }
                const bool hasParam = k > j;
                const int param = hasParam ? data.mid(j, k - j).toInt() : 0;
                if (k < n && data.at(k) == ' ')
                    ++k;
                i = k - 1;

                if (skipDestinations.contains(word)) {
                    flushBytes();
                    stack.last().skip = true;
                } else if (word == "par" || word == "line" || word == "sect" || word == "page") {
                    endParagraph(false);
                } else if (word == "row") {
                    endParagraph(true);
                    inTable = false;
                } else if (word == "intbl") {
                    inTable = true;
                } else if (word == "pard") {
                    inTable = false;
                } else if (word == "b" || word == "i" || word == "strike") {
                    const int flag = word == "b" ? RichText::Bold : (word == "i" ? RichText::Italic : RichText::Strike);
                    flushBytes();
                    stack.last().style = param == 0 && hasParam ? (stack.last().style & ~flag) : (stack.last().style | flag);
                } else if (word == "ul" || word == "ulnone" || word == "uld" || word == "uldb" || word == "ulw") {
                    flushBytes();
                    stack.last().style = (word == "ulnone" || (hasParam && param == 0))
                        ? (stack.last().style & ~RichText::Underline) : (stack.last().style | RichText::Underline);
                } else if (word == "plain") {
                    flushBytes();
                    stack.last().style = 0;
                } else if (word == "cell") {
                    emitText(QStringLiteral("\t"));
                } else if (word == "tab") {
                    emitText(QStringLiteral("\t"));
                } else if (word == "uc") {
                    stack.last().uc = param;
                } else if (word == "u") {
                    int code = param < 0 ? param + 65536 : param;
                    emitText(QString(QChar(char16_t(code))));
                    skipChars = stack.last().uc;
                } else if (word == "emdash") {
                    emitText(QString(QChar(0x2014)));
                } else if (word == "endash") {
                    emitText(QString(QChar(0x2013)));
                } else if (word == "bullet") {
                    emitText(QString(QChar(0x2022)));
                } else if (word == "lquote") {
                    emitText(QString(QChar(0x2018)));
                } else if (word == "rquote") {
                    emitText(QString(QChar(0x2019)));
                } else if (word == "ldblquote") {
                    emitText(QString(QChar(0x201C)));
                } else if (word == "rdblquote") {
                    emitText(QString(QChar(0x201D)));
                }
            } else {
                ++i; // unknown control symbol
            }
        } else if (c == '\r' || c == '\n') {
            continue;
        } else {
            if (skipChars > 0) {
                --skipChars;
                continue;
            }
            if (static_cast<unsigned char>(c) >= 0x80)
                pendingBytes.append(c);
            else
                emitText(QString(QLatin1Char(c)));
        }
    }
    flushBytes();
    return successRich(b);
}

} // namespace Importers
