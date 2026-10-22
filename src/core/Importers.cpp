#include "core/Importers.h"
#include "core/Images.h"
#include "core/TextUtils.h"

#include <QFile>
#include <QFileInfo>
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
            QStringLiteral("rtf"), QStringLiteral("pdf"), QStringLiteral("html"), QStringLiteral("htm")};
}

QString fileDialogFilter()
{
    QStringList patterns;
    for (const QString &s : supportedSuffixes())
        patterns << QStringLiteral("*.") + s;
    QStringList imagePatterns;
    for (const QString &s : Images::suffixes()) {
        patterns << QStringLiteral("*.") + s;
        imagePatterns << QStringLiteral("*.") + s;
    }
    return QObject::tr("Documenti supportati (%1);;Testo (*.txt *.md);;Word (*.docx);;OpenDocument (*.odt);;"
                       "RTF (*.rtf);;PDF (*.pdf);;HTML (*.html *.htm);;Immagini (%2);;Tutti i file (*)")
        .arg(patterns.join(QLatin1Char(' ')), imagePatterns.join(QLatin1Char(' ')));
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
    return success(decodeText(data));
}

Result importDocx(const QString &path)
{
    QString error;
    const QByteArray xml = zipEntry(path, QStringLiteral("word/document.xml"), &error);
    if (xml.isEmpty())
        return failure(error);

    QXmlStreamReader r(xml);
    QString out;
    bool inBody = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto name = r.name();
            if (name == QLatin1String("body"))
                inBody = true;
            else if (!inBody)
                continue;
            else if (name == QLatin1String("t"))
                out += r.readElementText();
            else if (name == QLatin1String("tab"))
                out += QLatin1Char('\t');
            else if (name == QLatin1String("br") || name == QLatin1String("cr"))
                out += QLatin1Char('\n');
            else if (name == QLatin1String("noBreakHyphen"))
                out += QChar(0x2011);
            else if (name == QLatin1String("delText") || name == QLatin1String("instrText"))
                r.skipCurrentElement();
        } else if (r.isEndElement() && inBody && r.name() == QLatin1String("p")) {
            out += QLatin1Char('\n');
        }
    }
    if (r.hasError())
        return failure(QObject::tr("Errore nel leggere il DOCX: %1").arg(r.errorString()));
    return success(out);
}

Result importOdt(const QString &path)
{
    QString error;
    const QByteArray xml = zipEntry(path, QStringLiteral("content.xml"), &error);
    if (xml.isEmpty())
        return failure(error);

    QXmlStreamReader r(xml);
    QString out;
    int paragraphDepth = 0;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto name = r.name();
            if (name == QLatin1String("p") || name == QLatin1String("h"))
                ++paragraphDepth;
            else if (name == QLatin1String("s")) { // text:c spaces, bounded: c="2000000000" would exhaust memory
                const int count = r.attributes().value(kOdfTextNamespace, QLatin1String("c")).toInt();
                out += QString(qBound(1, count, 1000), QLatin1Char(' '));
            }
            else if (name == QLatin1String("tab"))
                out += QLatin1Char('\t');
            else if (name == QLatin1String("line-break"))
                out += QLatin1Char('\n');
            else if (name == QLatin1String("note") || name == QLatin1String("annotation"))
                r.skipCurrentElement();
        } else if (r.isEndElement()) {
            const auto name = r.name();
            if ((name == QLatin1String("p") || name == QLatin1String("h")) && paragraphDepth > 0) {
                --paragraphDepth;
                if (paragraphDepth == 0)
                    out += QLatin1Char('\n');
            }
        } else if (r.isCharacters() && paragraphDepth > 0) {
            out += r.text();
        }
    }
    if (r.hasError())
        return failure(QObject::tr("Errore nel leggere l'ODT: %1").arg(r.errorString()));
    return success(out);
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
    return success(doc.toPlainText());
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
    };
    QList<State> stack{State{}};
    QString out;
    QByteArray pendingBytes; // \'hh sequences decoded together (multi-byte code pages)
    int skipChars = 0;
    QStringDecoder legacy(QStringDecoder::System);

    auto flushBytes = [&] {
        if (!pendingBytes.isEmpty()) {
            if (!stack.last().skip)
                out += legacy.decode(pendingBytes);
            pendingBytes.clear();
        }
    };
    auto emitText = [&](const QString &s) {
        flushBytes();
        if (!stack.last().skip)
            out += s;
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
                    emitText(QStringLiteral("\n"));
                } else if (word == "row") {
                    emitText(QStringLiteral("\n"));
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
    return success(out);
}

} // namespace Importers
